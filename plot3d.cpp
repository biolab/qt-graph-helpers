#include "plot3d.h"
#include <iostream>
#include <limits>
#include <QGLFormat>
#include <GL/glx.h>
#include <GL/glxext.h> // TODO: Windows?
#include <GL/glext.h>

PFNGLGENBUFFERSARBPROC glGenBuffers = NULL;
PFNGLBINDBUFFERPROC glBindBuffer = NULL;
PFNGLBUFFERDATAPROC glBufferData = NULL;
PFNGLENABLEVERTEXATTRIBARRAYARBPROC glEnableVertexAttribArray = NULL;
PFNGLDISABLEVERTEXATTRIBARRAYARBPROC glDisableVertexAttribArray = NULL;
typedef void (APIENTRYP PFNGLGETVERTEXATTRIBPOINTERPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer);
PFNGLGETVERTEXATTRIBPOINTERPROC glVertexAttribPointer = NULL;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = NULL;
PFNGLUNIFORM2FPROC glUniform2f = NULL;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = NULL;

#define BUFFER_OFFSET(i) ((char *)NULL + (i))

Plot3D::Plot3D(QWidget* parent) : QGLWidget(QGLFormat(QGL::SampleBuffers), parent)
{
    vbos_generated = false;
}

Plot3D::~Plot3D()
{
    if (vbos_generated)
    {
        glDeleteBuffers(1, &vbo_selected_id);
        glDeleteBuffers(1, &vbo_unselected_id);
        glDeleteBuffers(1, &vbo_edges_id);
    }
}

template<class T>
inline T clamp(T value, T min, T max)
{
    if (value > max)
        return max;
    if (value < min)
        return min;
    return value;
}

void Plot3D::set_symbol_geometry(int symbol, int type, const QList<QVector3D>& geometry)
{
    switch (type)
    {
        case 0:
            geometry_data_2d[symbol] = geometry;
            break;
        case 1:
            geometry_data_3d[symbol] = geometry;
            break;
        case 2:
            geometry_data_edges_2d[symbol] = geometry;
            break;
        case 3:
            geometry_data_edges_3d[symbol] = geometry;
            break;
        default:
            std::cout << "Wrong geometry type!" << std::endl;
    }
}

void Plot3D::set_data(quint64 array_address, int num_examples, int example_size)
{
    data_array = reinterpret_cast<float*>(array_address); // 32-bit systems, endianness?
    this->num_examples = num_examples;
    this->example_size = example_size;
    selected_indices = QVector<bool>(num_examples);

    // Load required extensions (OpenGL context should be up by now).
#ifdef _WIN32
    // TODO: wglGetProcAddress
#else
    glGenBuffers = (PFNGLGENBUFFERSARBPROC)glXGetProcAddress((const GLubyte*)"glGenBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC)glXGetProcAddress((const GLubyte*)"glBindBuffer");
    glBufferData = (PFNGLBUFFERDATAPROC)glXGetProcAddress((const GLubyte*)"glBufferData");
    glVertexAttribPointer = (PFNGLGETVERTEXATTRIBPOINTERPROC)glXGetProcAddress((const GLubyte*)"glVertexAttribPointer");
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYARBPROC)glXGetProcAddress((const GLubyte*)"glEnableVertexAttribArray");
    glDisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYARBPROC)glXGetProcAddress((const GLubyte*)"glDisableVertexAttribArray");
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)glXGetProcAddress((const GLubyte*)"glGetUniformLocation");
    glUniform2f = (PFNGLUNIFORM2FPROC)glXGetProcAddress((const GLubyte*)"glUniform2f");
    glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)glXGetProcAddress((const GLubyte*)"glDeleteBuffers");
#endif
}

void Plot3D::update_data(int x_index, int y_index, int z_index,
                         int color_index, int symbol_index, int size_index, int label_index,
                         const QList<QColor>& colors, int num_symbols_used,
                         bool x_discrete, bool y_discrete, bool z_discrete, bool use_2d_symbols)
{
    if (vbos_generated)
    {
        glDeleteBuffers(1, &vbo_selected_id);
        glDeleteBuffers(1, &vbo_unselected_id);
        glDeleteBuffers(1, &vbo_edges_id);
    }

    this->x_index = x_index;
    this->y_index = y_index;
    this->z_index = z_index;

    const float scale = 0.001;

    float* vbo_selected_data   = new float[num_examples * 144 * 13];
    float* vbo_unselected_data = new float[num_examples * 144 * 13];
    float* vbo_edges_data      = new float[num_examples * 144 * 13];
    float* dests = vbo_selected_data;
    float* destu = vbo_unselected_data;
    float* deste = vbo_edges_data;
    // Sizes in bytes.
    int sib_selected   = 0;
    int sib_unselected = 0;
    int sib_edges      = 0;

    QMap<int, QList<QVector3D> >& geometry = use_2d_symbols ? geometry_data_2d : geometry_data_3d;
    QMap<int, QList<QVector3D> >& geometry_edges = use_2d_symbols ? geometry_data_edges_2d : geometry_data_edges_3d;

    for (int index = 0; index < num_examples; ++index)
    {
        float* example = data_array + index*example_size;
        float x_pos = *(example + x_index);
        float y_pos = *(example + y_index);
        float z_pos = *(example + z_index);

        int symbol = 0;
        if (num_symbols_used > 1 && symbol_index > -1)
            symbol = *(example + symbol_index) * num_symbols_used;

        float size = *(example + size_index);
        if (size_index < 0 || size < 0.)
            size = 1.;

        float color_value = *(example + color_index);
        int num_colors = colors.count();
        QColor color;

        if (num_colors > 0)
            color = colors[clamp(int(color_value * num_colors), 0, num_colors-1)]; // TODO: garbage values sometimes?
        else if (color_index > -1)
            color = QColor(0., 0., color_value);
        else
            color = QColor(0., 0., 0.8);

        float*& dest = selected_indices[index] ? dests : destu;

        // TODO: make sure symbol is in geometry map
        for (int i = 0; i < geometry[symbol].count(); i += 6) {
            if (selected_indices[index])
                sib_selected += 3*13*4;
            else
                sib_unselected += 3*13*4;

            for (int j = 0; j < 3; ++j)
            {
                // position
                *dest = x_pos; dest++; 
                *dest = y_pos; dest++; 
                *dest = z_pos; dest++; 

                // offset
                *dest = geometry[symbol][i+j].x()*size*scale; dest++;
                *dest = geometry[symbol][i+j].y()*size*scale; dest++;
                *dest = geometry[symbol][i+j].z()*size*scale; dest++;

                // color
                *dest = color.redF(); dest++;
                *dest = color.greenF(); dest++;
                *dest = color.blueF(); dest++;

                // normal
                *dest = geometry[symbol][i+3+j].x(); dest++;
                *dest = geometry[symbol][i+3+j].y(); dest++;
                *dest = geometry[symbol][i+3+j].z(); dest++;

                // index
                *dest = index; dest++;
            }
        }

        // No need for edges in selected examples (those are drawn fully opaque)
        if (selected_indices[index])
            continue;

        for (int i = 0; i < geometry_edges[symbol].count(); i += 2) {
            sib_edges += 2*13*4;

            for (int j = 0; j < 2; ++j)
            {
                *deste = x_pos; deste++; 
                *deste = y_pos; deste++; 
                *deste = z_pos; deste++; 

                *deste = geometry_edges[symbol][i+j].x()*size*scale; deste++;
                *deste = geometry_edges[symbol][i+j].y()*size*scale; deste++;
                *deste = geometry_edges[symbol][i+j].z()*size*scale; deste++;

                *deste = color.redF(); deste++;
                *deste = color.greenF(); deste++;
                *deste = color.blueF(); deste++;

                // Just use offset as the normal for now
                *deste = geometry_edges[symbol][i+j].x(); deste++;
                *deste = geometry_edges[symbol][i+j].y(); deste++;
                *deste = geometry_edges[symbol][i+j].z(); deste++;

                *deste = index; deste++;
            }
        }
    }

    glGenBuffers(1, &vbo_selected_id);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_selected_id);
    glBufferData(GL_ARRAY_BUFFER, sib_selected, vbo_selected_data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    delete [] vbo_selected_data;

    glGenBuffers(1, &vbo_unselected_id);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_unselected_id);
    glBufferData(GL_ARRAY_BUFFER, sib_unselected, vbo_unselected_data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    delete [] vbo_unselected_data;

    glGenBuffers(1, &vbo_edges_id);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_edges_id);
    glBufferData(GL_ARRAY_BUFFER, sib_edges, vbo_edges_data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    delete [] vbo_edges_data;

    num_selected_vertices = sib_selected / (13*4);
    num_unselected_vertices = sib_unselected / (13*4);
    num_edges_vertices = sib_edges / (13*4);

    vbos_generated = true;
}

void Plot3D::draw_data_solid()
{
    // TODO
}

void Plot3D::draw_data(GLuint shader_id, float alpha_value)
{
    if (!vbos_generated)
        return;

    // Draw opaque selected examples first.
    glBindBuffer(GL_ARRAY_BUFFER, vbo_selected_id);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(0));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(3*4));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(6*4));
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(9*4));
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(12*4));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);

    glDrawArrays(GL_TRIANGLES, 0, num_selected_vertices);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);

    // Draw transparent unselected examples (triangles and then edges).
    glUniform2f(glGetUniformLocation(shader_id, "alpha_value"), alpha_value-0.6, alpha_value-0.6);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_unselected_id);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(0));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(3*4));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(6*4));
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(9*4));
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(12*4));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);

    glDrawArrays(GL_TRIANGLES, 0, num_unselected_vertices);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);

    // Edges
    glUniform2f(glGetUniformLocation(shader_id, "alpha_value"), alpha_value, alpha_value);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_edges_id);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(0));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(3*4));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(6*4));
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(9*4));
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 13*4, BUFFER_OFFSET(12*4));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);

    glDrawArrays(GL_LINES, 0, num_edges_vertices);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glDisableVertexAttribArray(3);
    glDisableVertexAttribArray(4);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

QList<double> Plot3D::get_min_max_selected(const QList<int>& area,
                                           const QMatrix4x4& mvp,
                                           const QList<int>& viewport,
                                           const QVector3D& plot_scale,
                                           const QVector3D& plot_translation)
{
    float x_min = std::numeric_limits<float>::max();
    float x_max = std::numeric_limits<float>::min();
    float y_min = x_min;
    float y_max = x_max;
    float z_min = x_min;
    float z_max = x_max;

    bool any_point_selected = false;
    for (int index = 0; index < num_examples; ++index)
    {
        float* example = data_array + index*example_size;
        float x_pos = *(example + x_index);
        float y_pos = *(example + y_index);
        float z_pos = *(example + z_index);

        QVector3D position(x_pos, y_pos, z_pos);
        position += plot_translation;
        position *= plot_scale;

        QVector4D projected = mvp * QVector4D(position, 1.0f);
        projected /= projected.z();
        int winx = viewport[0] + (1 + projected.x()) * viewport[2] / 2;
        int winy = viewport[1] + (1 + projected.y()) * viewport[3] / 2;
        winy = viewport[3] - winy;

        if (winx >= area[0] && winx <= area[0]+area[2] && winy <= area[1]+area[3] && winy >= area[1])
        {
            any_point_selected = true;

            if (x_pos < x_min) x_min = x_pos;
            if (x_pos > x_max) x_max = x_pos;

            if (y_pos < y_min) y_min = y_pos;
            if (y_pos > y_max) y_max = y_pos;

            if (z_pos < z_min) z_min = z_pos;
            if (z_pos > z_max) z_max = z_pos;
        }
    }

    if (any_point_selected)
    {
        QList<double> min_max;
        min_max << x_min << x_max;
        min_max << y_min << y_max;
        min_max << z_min << z_max;
        return min_max;
    }
    else
    {
        QList<double> min_max;
        min_max << 0. << 1.;
        min_max << 0. << 1.;
        min_max << 0. << 1.;
        return min_max;
    }
}

void Plot3D::select_points(const QList<int>& area,
                           const QMatrix4x4& mvp,
                           const QList<int>& viewport,
                           const QVector3D& plot_scale,
                           const QVector3D& plot_translation,
                           Plot::SelectionBehavior behavior)
{
    if (behavior == Plot::ReplaceSelection)
        selected_indices.fill(false);

    for (int index = 0; index < num_examples; ++index)
    {
        float* example = data_array + index*example_size;
        float x_pos = *(example + x_index);
        float y_pos = *(example + y_index);
        float z_pos = *(example + z_index);

        QVector3D position(x_pos, y_pos, z_pos);
        position += plot_translation;
        position *= plot_scale;

        QVector4D projected = mvp * QVector4D(position, 1.0f);
        projected /= projected.z();
        int winx = viewport[0] + (1 + projected.x()) * viewport[2] / 2;
        int winy = viewport[1] + (1 + projected.y()) * viewport[3] / 2;
        winy = viewport[3] - winy;

        if (winx >= area[0] && winx <= area[0]+area[2] && winy <= area[1]+area[3] && winy >= area[1])
        {
            if (behavior == Plot::AddSelection || behavior == Plot::ReplaceSelection)
                selected_indices[index] = true;
            else if (behavior == Plot::RemoveSelection)
                selected_indices[index] = false;
            else if (behavior == Plot::ToggleSelection)
                selected_indices[index] = !selected_indices[index];
        }
    }
}

void Plot3D::unselect_all_points()
{
    selected_indices = QVector<bool>(num_examples);
}

QList<bool> Plot3D::get_selected_indices()
{
    //return selected_indices.toList(); // TODO: this crashes on adult.tab
    return QList<bool>();
}

#include "plot3d.moc"
