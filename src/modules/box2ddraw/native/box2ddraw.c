#include <athena/box2ddraw.h>
#include <athena/color.h>
#include <athena/graphics.h>

#define LINE_BATCH 256
#define FILL_ALPHA 0x30
#define TRANSFORM_AXIS 0.5f /* meters */

typedef struct DrawContext {
    AthenaBox2DDrawOptions options;
    prim_line lines[LINE_BATCH];
    int line_count;
} DrawContext;

static Color hex_color(b2HexColor color, uint32_t alpha) {
    return athena_color_new((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, alpha);
}

static float screen_x(const DrawContext *draw, float x) {
    return draw->options.offsetX + x * draw->options.scale;
}

static float screen_y(const DrawContext *draw, float y) {
    return draw->options.flipY ? draw->options.offsetY - y * draw->options.scale
                               : draw->options.offsetY + y * draw->options.scale;
}

static void flush_lines(DrawContext *draw) {
    if (draw->line_count > 0)
        draw_line_list(0.0f, 0.0f, draw->lines, draw->line_count);
    draw->line_count = 0;
}

/* World-space segment, batched. */
static void add_line(DrawContext *draw, b2Vec2 p1, b2Vec2 p2, Color color) {
    prim_line *line;

    if (draw->line_count == LINE_BATCH)
        flush_lines(draw);
    line = &draw->lines[draw->line_count++];
    line->x = screen_x(draw, p1.x);
    line->y = screen_y(draw, p1.y);
    line->x2 = screen_x(draw, p2.x);
    line->y2 = screen_y(draw, p2.y);
    line->rgba = color;
}

static void add_polygon(DrawContext *draw, b2Transform transform, const b2Vec2 *vertices, int count, Color color) {
    b2Vec2 previous = b2TransformPoint(transform, vertices[count - 1]);

    for (int i = 0; i < count; i++) {
        b2Vec2 current = b2TransformPoint(transform, vertices[i]);
        add_line(draw, previous, current, color);
        previous = current;
    }
}

static void draw_polygon_cb(b2Transform transform, const b2Vec2 *vertices, int count, b2HexColor color,
    void *context) {
    add_polygon(context, transform, vertices, count, hex_color(color, ATHENA_COLOR_DEFAULT_ALPHA));
}

static void draw_solid_polygon_cb(b2Transform transform, const b2Vec2 *vertices, int count, float radius,
    b2HexColor color, void *context) {
    DrawContext *draw = context;

    (void)radius; /* rounded corners are drawn as the core polygon */
    if (draw->options.fill && count >= 3) {
        Color fill = hex_color(color, FILL_ALPHA);
        b2Vec2 a = b2TransformPoint(transform, vertices[0]);
        /* Triangles are drawn right away: flush first to keep the draw order. */
        flush_lines(draw);
        for (int i = 1; i + 1 < count; i++) {
            b2Vec2 b = b2TransformPoint(transform, vertices[i]);
            b2Vec2 c = b2TransformPoint(transform, vertices[i + 1]);
            draw_triangle(screen_x(draw, a.x), screen_y(draw, a.y), screen_x(draw, b.x), screen_y(draw, b.y),
                screen_x(draw, c.x), screen_y(draw, c.y), fill);
        }
    }
    add_polygon(draw, transform, vertices, count, hex_color(color, ATHENA_COLOR_DEFAULT_ALPHA));
}

static void circle(DrawContext *draw, b2Vec2 center, float radius, b2HexColor color) {
    flush_lines(draw);
    if (draw->options.fill)
        draw_circle(screen_x(draw, center.x), screen_y(draw, center.y), radius * draw->options.scale,
            hex_color(color, FILL_ALPHA), 1);
    draw_circle(screen_x(draw, center.x), screen_y(draw, center.y), radius * draw->options.scale,
        hex_color(color, ATHENA_COLOR_DEFAULT_ALPHA), 0);
}

static void draw_circle_cb(b2Vec2 center, float radius, b2HexColor color, void *context) {
    circle(context, center, radius, color);
}

static void draw_solid_circle_cb(b2Transform transform, b2Vec2 center, float radius, b2HexColor color,
    void *context) {
    b2Vec2 world_center = b2TransformPoint(transform, center);
    b2Vec2 edge = b2MulAdd(world_center, radius, b2Rot_GetXAxis(transform.q));

    circle(context, world_center, radius, color);
    /* The radius line shows the rotation. */
    add_line(context, world_center, edge, hex_color(color, ATHENA_COLOR_DEFAULT_ALPHA));
}

static void draw_solid_capsule_cb(b2Vec2 p1, b2Vec2 p2, float radius, b2HexColor color, void *context) {
    DrawContext *draw = context;
    Color outline = hex_color(color, ATHENA_COLOR_DEFAULT_ALPHA);
    b2Vec2 axis = b2Normalize(b2Sub(p2, p1));
    b2Vec2 side = b2MulSV(radius, b2LeftPerp(axis));

    circle(draw, p1, radius, color);
    circle(draw, p2, radius, color);
    add_line(draw, b2Add(p1, side), b2Add(p2, side), outline);
    add_line(draw, b2Sub(p1, side), b2Sub(p2, side), outline);
}

static void draw_line_cb(b2Vec2 p1, b2Vec2 p2, b2HexColor color, void *context) {
    add_line(context, p1, p2, hex_color(color, ATHENA_COLOR_DEFAULT_ALPHA));
}

static void draw_transform_cb(b2Transform transform, void *context) {
    b2Vec2 x = b2MulAdd(transform.p, TRANSFORM_AXIS, b2Rot_GetXAxis(transform.q));
    b2Vec2 y = b2MulAdd(transform.p, TRANSFORM_AXIS, b2Rot_GetYAxis(transform.q));

    add_line(context, transform.p, x, hex_color(b2_colorRed, ATHENA_COLOR_DEFAULT_ALPHA));
    add_line(context, transform.p, y, hex_color(b2_colorGreen, ATHENA_COLOR_DEFAULT_ALPHA));
}

/* `size` is in pixels. */
static void draw_point_cb(b2Vec2 p, float size, b2HexColor color, void *context) {
    DrawContext *draw = context;
    int pixels = size < 1.0f ? 1 : (int)size;

    flush_lines(draw);
    draw_sprite(screen_x(draw, p.x) - pixels * 0.5f, screen_y(draw, p.y) - pixels * 0.5f, pixels, pixels,
        hex_color(color, ATHENA_COLOR_DEFAULT_ALPHA));
}

static void draw_bounds_cb(b2AABB aabb, b2HexColor color, void *context) {
    Color outline = hex_color(color, ATHENA_COLOR_DEFAULT_ALPHA);
    b2Vec2 a = aabb.lowerBound, c = aabb.upperBound;
    b2Vec2 b = { c.x, a.y }, d = { a.x, c.y };

    add_line(context, a, b, outline);
    add_line(context, b, c, outline);
    add_line(context, c, d, outline);
    add_line(context, d, a, outline);
}

void athena_box2d_draw_defaults(AthenaBox2DDrawOptions *options) {
    GSCONTEXT *gs = getGSGLOBAL();

    *options = (AthenaBox2DDrawOptions){ 0 };
    options->scale = 32.0f;
    options->offsetX = gs ? gs->Width * 0.5f : 320.0f;
    options->offsetY = gs ? gs->Height * 0.5f : 224.0f;
    options->flipY = true;
    options->shapes = true;
    options->joints = true;
}

/* The world rectangle on screen, so Box2D skips everything else. */
static b2AABB screen_bounds(const DrawContext *draw, const GSCONTEXT *gs) {
    float x1 = (0.0f - draw->options.offsetX) / draw->options.scale;
    float x2 = ((float)gs->Width - draw->options.offsetX) / draw->options.scale;
    float y1 = (0.0f - draw->options.offsetY) / draw->options.scale;
    float y2 = ((float)gs->Height - draw->options.offsetY) / draw->options.scale;
    b2AABB bounds;

    if (draw->options.flipY) {
        y1 = -y1;
        y2 = -y2;
    }
    bounds.lowerBound = (b2Vec2){ b2MinFloat(x1, x2), b2MinFloat(y1, y2) };
    bounds.upperBound = (b2Vec2){ b2MaxFloat(x1, x2), b2MaxFloat(y1, y2) };
    return bounds;
}

bool athena_box2d_draw(b2WorldId world, const AthenaBox2DDrawOptions *options) {
    /* Too large for the stack of a script thread. */
    static DrawContext draw;
    GSCONTEXT *gs = getGSGLOBAL();
    b2DebugDraw debug = b2DefaultDebugDraw();

    if (!gs)
        return false;
    draw.options = *options;
    draw.line_count = 0;

    debug.DrawPolygonFcn = draw_polygon_cb;
    debug.DrawSolidPolygonFcn = draw_solid_polygon_cb;
    debug.DrawCircleFcn = draw_circle_cb;
    debug.DrawSolidCircleFcn = draw_solid_circle_cb;
    debug.DrawSolidCapsuleFcn = draw_solid_capsule_cb;
    debug.DrawLineFcn = draw_line_cb;
    debug.DrawTransformFcn = draw_transform_cb;
    debug.DrawPointFcn = draw_point_cb;
    debug.DrawBoundsFcn = draw_bounds_cb;
    debug.drawingBounds = screen_bounds(&draw, gs);
    debug.drawShapes = options->shapes;
    debug.drawJoints = options->joints;
    debug.drawJointExtras = options->jointExtras;
    debug.drawBounds = options->bounds;
    debug.drawContacts = options->contacts;
    debug.drawContactNormals = options->contacts;
    debug.drawMass = options->mass;
    debug.context = &draw;

    b2World_Draw(world, &debug);
    flush_lines(&draw);
    return true;
}
