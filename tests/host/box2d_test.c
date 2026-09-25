/*
 * Host test of src/modules/box2d/native/box2d.c against the vendored
 * Box2D 3.2 (src/modules/box2d/native/box2d/), simulated for real:
 *
 * - the validity rules reject exactly the input Box2D would assert on;
 * - joint frames built from a world anchor and axis start relaxed, also
 *   between rotated bodies;
 * - the joint parameter dispatch covers every joint type, and refuses the
 *   parameters a type does not have;
 * - B2_MAX_WORLDS (lowered for the EE) is enforced without an assertion.
 *
 * Every Box2D assertion fails the test instead of trapping.
 * Run with tests/host/run.sh from the repository root.
 */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <athena/box2d.h>

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
	printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define NEAR(a, b, tolerance) (fabsf((a) - (b)) <= (tolerance))

static int assertions;

static int count_assertion(const char *condition, const char *file, int line)
{
	assertions++;
	printf("  FAIL Box2D assertion: %s (%s:%d)\n", condition, file, line);
	return 0; /* do not trap */
}

static b2WorldId new_world(b2Vec2 gravity)
{
	b2WorldDef def = b2DefaultWorldDef();
	def.gravity = gravity;
	return b2CreateWorld(&def);
}

static b2BodyId new_body(b2WorldId world, b2BodyType type, b2Vec2 position, float angle)
{
	b2BodyDef def = b2DefaultBodyDef();
	b2Polygon box = b2MakeBox(0.5f, 0.5f);
	b2ShapeDef shape = b2DefaultShapeDef();
	b2BodyId body;

	def.type = type;
	def.position = position;
	def.rotation = b2MakeRot(angle);
	body = b2CreateBody(world, &def);
	b2CreatePolygonShape(body, &shape, &box);
	return body;
}

static void step(b2WorldId world, int count)
{
	for (int i = 0; i < count; i++)
		b2World_Step(world, 1.0f / 60.0f, 4);
}

static void test_names(void)
{
	CHECK(!strcmp(athena_box2d_shape_type_name(b2_chainSegmentShape), "chainSegment"), "chain segment name");
	CHECK(!strcmp(athena_box2d_joint_type_name(b2_wheelJoint), "wheel"), "wheel name");
	CHECK(!strcmp(athena_box2d_joint_type_name((b2JointType)99), "unknown"), "unknown joint name");
}

static void test_geometry(void)
{
	float slop = athena_box2d_linear_slop();
	b2Polygon polygon;
	b2Vec2 square[] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	b2Vec2 collinear[] = { { 0, 0 }, { 1, 0 }, { 2, 0 } };
	b2Vec2 welded[] = { { 0, 0 }, { slop * 0.1f, 0 }, { 0, slop * 0.1f } };
	b2Vec2 many[B2_MAX_POLYGON_VERTICES + 1];
	b2Vec2 chain[] = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 1 } };
	b2Vec2 chain_repeat[] = { { 0, 0 }, { 1, 0 }, { 1, 0 }, { 3, 1 } };
	b2Vec2 loop_closed[] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 } };

	CHECK(slop > 0.0f && slop < 0.1f, "linear slop %g", (double)slop);

	CHECK(athena_box2d_make_box(1.0f, 0.5f, (b2Vec2){ 2, 3 }, 0.5f, &polygon), "box");
	CHECK(polygon.count == 4 && NEAR(polygon.centroid.x, 2.0f, 1e-5f) && NEAR(polygon.centroid.y, 3.0f, 1e-5f),
		"box centroid (%g, %g)", (double)polygon.centroid.x, (double)polygon.centroid.y);
	CHECK(!athena_box2d_make_box(0.0f, 1.0f, b2Vec2_zero, 0.0f, &polygon), "zero width box");
	CHECK(!athena_box2d_make_box(-1.0f, 1.0f, b2Vec2_zero, 0.0f, &polygon), "negative box");
	CHECK(!athena_box2d_make_box(1e-5f, 1e-5f, b2Vec2_zero, 0.0f, &polygon), "box without mass area");
	CHECK(!athena_box2d_make_box(1.0f, 1.0f, b2Vec2_zero, NAN, &polygon), "box with NaN angle");

	CHECK(athena_box2d_make_polygon(square, 4, 0.1f, &polygon) && polygon.count == 4 &&
		NEAR(polygon.radius, 0.1f, 1e-6f), "square polygon");
	CHECK(!athena_box2d_make_polygon(square, 2, 0.0f, &polygon), "two points");
	CHECK(!athena_box2d_make_polygon(collinear, 3, 0.0f, &polygon), "collinear points");
	CHECK(!athena_box2d_make_polygon(welded, 3, 0.0f, &polygon), "points closer than the slop");
	CHECK(!athena_box2d_make_polygon(square, 4, -1.0f, &polygon), "negative radius");
	for (int i = 0; i < B2_MAX_POLYGON_VERTICES + 1; i++)
		many[i] = (b2Vec2){ cosf(i * 0.6f), sinf(i * 0.6f) };
	CHECK(!athena_box2d_make_polygon(many, B2_MAX_POLYGON_VERTICES + 1, 0.0f, &polygon), "too many points");
	square[2].x = INFINITY;
	CHECK(!athena_box2d_make_polygon(square, 4, 0.0f, &polygon), "infinite point");

	CHECK(athena_box2d_segment_valid((b2Vec2){ 0, 0 }, (b2Vec2){ 1, 0 }), "segment");
	CHECK(!athena_box2d_segment_valid((b2Vec2){ 0, 0 }, (b2Vec2){ slop * 0.5f, 0 }), "short segment");
	CHECK(!athena_box2d_segment_valid((b2Vec2){ 0, 0 }, (b2Vec2){ NAN, 0 }), "NaN segment");

	CHECK(athena_box2d_chain_valid(chain, 4, false), "open chain");
	CHECK(!athena_box2d_chain_valid(chain, 3, false), "three chain points");
	CHECK(!athena_box2d_chain_valid(chain_repeat, 4, false), "repeated chain point");
	CHECK(athena_box2d_chain_valid(loop_closed, 4, false), "open chain returning to the start");
	CHECK(!athena_box2d_chain_valid(loop_closed, 4, true), "loop with a repeated closing point");
}

static void test_shapes_in_world(void)
{
	b2WorldId world = new_world((b2Vec2){ 0, -10 });
	b2BodyId ground = new_body(world, b2_staticBody, b2Vec2_zero, 0.0f);
	b2BodyId body = new_body(world, b2_dynamicBody, (b2Vec2){ 0, 5 }, 0.0f);
	b2ShapeDef def = b2DefaultShapeDef();
	b2Polygon polygon;
	b2Vec2 triangle[] = { { -1, 0 }, { 1, 0 }, { 0, 1 } };
	b2Vec2 points[] = { { -10, 1 }, { -5, 0 }, { 5, 0 }, { 10, 1 } };
	b2ChainDef chain = b2DefaultChainDef();
	b2ChainId chain_id;

	CHECK(athena_box2d_make_polygon(triangle, 3, 0.0f, &polygon), "triangle");
	CHECK(B2_IS_NON_NULL(b2CreatePolygonShape(body, &def, &polygon)), "triangle shape");
	CHECK(athena_box2d_make_box(0.1f, 0.1f, (b2Vec2){ 0, 2 }, 0.3f, &polygon), "offset box");
	CHECK(B2_IS_NON_NULL(b2CreatePolygonShape(body, &def, &polygon)), "offset box shape");

	chain.points = points;
	chain.count = 4;
	CHECK(athena_box2d_chain_valid(points, 4, false), "chain points");
	chain_id = b2CreateChain(ground, &chain);
	/* An open chain uses the end points as ghosts: 4 points, 1 segment. */
	CHECK(b2Chain_GetSegmentCount(chain_id) == 1, "chain segments %d", b2Chain_GetSegmentCount(chain_id));

	step(world, 120);
	CHECK(b2Body_GetPosition(body).y < 5.0f, "body fell to %g", (double)b2Body_GetPosition(body).y);
	b2DestroyWorld(world);
}

/* Revolute and weld: anchors between rotated bodies start with zero relative angle. */
static void test_frames_angle(void)
{
	b2WorldId world = new_world(b2Vec2_zero);
	b2BodyId a = new_body(world, b2_dynamicBody, (b2Vec2){ 0, 0 }, 0.7f);
	b2BodyId b = new_body(world, b2_dynamicBody, (b2Vec2){ 2, 0 }, -0.4f);
	b2Vec2 anchor = { 1, 0 };
	b2RevoluteJointDef revolute = b2DefaultRevoluteJointDef();
	b2WeldJointDef weld = b2DefaultWeldJointDef();
	b2JointId joint;
	b2Vec2 pa, pb;
	float angle;

	revolute.base.bodyIdA = a;
	revolute.base.bodyIdB = b;
	athena_box2d_joint_frames(&revolute.base, &anchor, b2Rot_identity);
	pa = b2Body_GetWorldPoint(a, revolute.base.localFrameA.p);
	pb = b2Body_GetWorldPoint(b, revolute.base.localFrameB.p);
	CHECK(NEAR(pa.x, 1.0f, 1e-5f) && NEAR(pa.y, 0.0f, 1e-5f) && NEAR(pb.x, 1.0f, 1e-5f) && NEAR(pb.y, 0.0f, 1e-5f),
		"revolute anchors at the world point: (%g, %g) (%g, %g)", (double)pa.x, (double)pa.y, (double)pb.x, (double)pb.y);
	joint = b2CreateRevoluteJoint(world, &revolute);
	CHECK(athena_box2d_joint_get(joint, ATHENA_BOX2D_ANGLE, &angle) && NEAR(angle, 0.0f, 1e-4f),
		"revolute starts at angle %g", (double)angle);
	step(world, 30);
	CHECK(athena_box2d_joint_get(joint, ATHENA_BOX2D_ANGLE, &angle) && NEAR(angle, 0.0f, 1e-3f),
		"revolute stays relaxed: %g", (double)angle);
	b2DestroyJoint(joint, false);

	weld.base.bodyIdA = a;
	weld.base.bodyIdB = b;
	athena_box2d_joint_frames(&weld.base, NULL, b2Rot_identity);
	CHECK(NEAR(b2Length(weld.base.localFrameA.p), 0.0f, 1e-6f) && NEAR(b2Length(weld.base.localFrameB.p), 0.0f, 1e-6f),
		"without anchor the frames sit at the body origins");
	b2CreateWeldJoint(world, &weld);
	b2Body_SetAngularVelocity(b, 1.0f);
	step(world, 60);
	angle = b2RelativeAngle(b2Body_GetRotation(a), b2Body_GetRotation(b));
	CHECK(NEAR(angle, -1.1f, 0.05f), "weld keeps the initial relative angle: %g", (double)angle);
	b2DestroyWorld(world);
}

/* Prismatic: a world axis between rotated bodies; motion only along it. */
static void test_frames_axis(void)
{
	b2WorldId world = new_world((b2Vec2){ 0, -10 });
	b2BodyId ground = new_body(world, b2_staticBody, b2Vec2_zero, 0.3f);
	b2BodyId slider = new_body(world, b2_dynamicBody, (b2Vec2){ 0, 3 }, 1.0f);
	b2PrismaticJointDef def = b2DefaultPrismaticJointDef();
	b2Vec2 anchor = { 0, 3 };
	b2Vec2 axis = b2Normalize((b2Vec2){ 1, 1 });
	b2JointId joint;
	b2Vec2 position;
	float translation;

	def.base.bodyIdA = ground;
	def.base.bodyIdB = slider;
	athena_box2d_joint_frames(&def.base, &anchor, b2MakeRotFromUnitVector(axis));
	joint = b2CreatePrismaticJoint(world, &def);
	CHECK(athena_box2d_joint_get(joint, ATHENA_BOX2D_TRANSLATION, &translation) && NEAR(translation, 0.0f, 1e-4f),
		"prismatic starts at translation %g", (double)translation);

	step(world, 60);
	position = b2Body_GetPosition(slider);
	/* Gravity pulls it down the diagonal: x and y move by the same amount. */
	CHECK(position.y < 2.0f && NEAR(position.x, position.y - 3.0f, 0.02f),
		"slider moved along the axis to (%g, %g)", (double)position.x, (double)position.y);
	CHECK(NEAR(b2Rot_GetAngle(b2Body_GetRotation(slider)), 1.0f, 1e-3f), "slider keeps its rotation");
	athena_box2d_joint_get(joint, ATHENA_BOX2D_TRANSLATION, &translation);
	CHECK(translation < -1.0f, "negative translation along the axis: %g", (double)translation);
	b2DestroyWorld(world);
}

/* Which joint types have which parameter. */
static const b2JointType joint_types[] = {
	b2_distanceJoint, b2_filterJoint, b2_motorJoint, b2_prismaticJoint, b2_revoluteJoint, b2_weldJoint, b2_wheelJoint,
};

static bool supports(b2JointType type, AthenaBox2DJointParam param)
{
	bool spring = type == b2_distanceJoint || type == b2_revoluteJoint || type == b2_prismaticJoint ||
		type == b2_wheelJoint;

	switch (param) {
	case ATHENA_BOX2D_SPRING_ENABLED: case ATHENA_BOX2D_SPRING_HERTZ: case ATHENA_BOX2D_SPRING_DAMPING_RATIO:
	case ATHENA_BOX2D_LIMIT_ENABLED: case ATHENA_BOX2D_LOWER_LIMIT: case ATHENA_BOX2D_UPPER_LIMIT:
	case ATHENA_BOX2D_MOTOR_ENABLED: case ATHENA_BOX2D_MOTOR_SPEED:
		return spring;
	case ATHENA_BOX2D_MAX_MOTOR_FORCE: case ATHENA_BOX2D_MOTOR_FORCE:
		return type == b2_distanceJoint || type == b2_prismaticJoint;
	case ATHENA_BOX2D_MAX_MOTOR_TORQUE: case ATHENA_BOX2D_MOTOR_TORQUE:
		return type == b2_revoluteJoint || type == b2_wheelJoint;
	case ATHENA_BOX2D_LINEAR_HERTZ: case ATHENA_BOX2D_LINEAR_DAMPING_RATIO:
	case ATHENA_BOX2D_ANGULAR_HERTZ: case ATHENA_BOX2D_ANGULAR_DAMPING_RATIO:
		return type == b2_weldJoint || type == b2_motorJoint;
	case ATHENA_BOX2D_ANGULAR_VELOCITY: case ATHENA_BOX2D_MAX_VELOCITY_FORCE: case ATHENA_BOX2D_MAX_VELOCITY_TORQUE:
	case ATHENA_BOX2D_MAX_SPRING_FORCE: case ATHENA_BOX2D_MAX_SPRING_TORQUE:
		return type == b2_motorJoint;
	case ATHENA_BOX2D_LENGTH: case ATHENA_BOX2D_CURRENT_LENGTH:
		return type == b2_distanceJoint;
	case ATHENA_BOX2D_ANGLE:
		return type == b2_revoluteJoint;
	case ATHENA_BOX2D_TRANSLATION: case ATHENA_BOX2D_SPEED:
		return type == b2_prismaticJoint;
	default:
		return false;
	}
}

static bool read_only(AthenaBox2DJointParam param)
{
	return param == ATHENA_BOX2D_LOWER_LIMIT || param == ATHENA_BOX2D_UPPER_LIMIT ||
		param == ATHENA_BOX2D_MOTOR_FORCE || param == ATHENA_BOX2D_MOTOR_TORQUE ||
		param == ATHENA_BOX2D_CURRENT_LENGTH || param == ATHENA_BOX2D_ANGLE ||
		param == ATHENA_BOX2D_TRANSLATION || param == ATHENA_BOX2D_SPEED;
}

static bool flag_param(AthenaBox2DJointParam param)
{
	return param == ATHENA_BOX2D_SPRING_ENABLED || param == ATHENA_BOX2D_LIMIT_ENABLED ||
		param == ATHENA_BOX2D_MOTOR_ENABLED;
}

static b2JointId create_joint(b2WorldId world, b2JointType type, b2BodyId a, b2BodyId b)
{
#define CREATE(Kind) do { b2##Kind##Def def = b2Default##Kind##Def(); \
	def.base.bodyIdA = a; def.base.bodyIdB = b; return b2Create##Kind(world, &def); } while (0)
	switch (type) {
	case b2_distanceJoint: CREATE(DistanceJoint);
	case b2_filterJoint: CREATE(FilterJoint);
	case b2_motorJoint: CREATE(MotorJoint);
	case b2_prismaticJoint: CREATE(PrismaticJoint);
	case b2_revoluteJoint: CREATE(RevoluteJoint);
	case b2_weldJoint: CREATE(WeldJoint);
	default: CREATE(WheelJoint);
	}
#undef CREATE
}

static void test_joint_params(void)
{
	b2WorldId world = new_world(b2Vec2_zero);
	b2BodyId a = new_body(world, b2_dynamicBody, (b2Vec2){ 0, 0 }, 0.0f);
	b2BodyId b = new_body(world, b2_dynamicBody, (b2Vec2){ 2, 0 }, 0.0f);

	for (size_t t = 0; t < sizeof(joint_types) / sizeof(joint_types[0]); t++) {
		b2JointType type = joint_types[t];
		b2JointId joint = create_joint(world, type, a, b);
		const char *name = athena_box2d_joint_type_name(type);

		CHECK(b2Joint_GetType(joint) == type, "created a %s joint", name);
		for (int p = 0; p < ATHENA_BOX2D_JOINT_PARAM_COUNT; p++) {
			AthenaBox2DJointParam param = (AthenaBox2DJointParam)p;
			bool expected = supports(type, param);
			float value = -123.0f, written = flag_param(param) ? 1.0f : 1.25f;

			CHECK(athena_box2d_joint_get(joint, param, &value) == expected,
				"get param %d on a %s joint", p, name);
			if (!expected)
				CHECK(value == -123.0f, "unsupported get leaves the output alone (%s, %d)", name, p);
			CHECK(athena_box2d_joint_set(joint, param, written) == (expected && !read_only(param)),
				"set param %d on a %s joint", p, name);
			if (expected && !read_only(param)) {
				athena_box2d_joint_get(joint, param, &value);
				CHECK(value == written, "param %d on a %s joint reads back %g", p, name, (double)value);
			}
		}

		switch (type) {
		case b2_revoluteJoint: {
			float lower, upper;
			CHECK(athena_box2d_joint_set_limits(joint, -1.0f, 2.0f), "revolute limits");
			athena_box2d_joint_get(joint, ATHENA_BOX2D_LOWER_LIMIT, &lower);
			athena_box2d_joint_get(joint, ATHENA_BOX2D_UPPER_LIMIT, &upper);
			CHECK(lower == -1.0f && upper == 2.0f, "revolute limits read back %g %g", (double)lower, (double)upper);
			CHECK(!athena_box2d_joint_set_limits(joint, -1.0f, 3.5f), "revolute limit beyond 0.99 pi");
			CHECK(!athena_box2d_joint_set_limits(joint, 1.0f, -1.0f), "revolute lower > upper");
			break;
		}
		case b2_distanceJoint:
		case b2_prismaticJoint:
		case b2_wheelJoint:
			CHECK(athena_box2d_joint_set_limits(joint, 0.5f, 5.0f), "%s limits", name);
			CHECK(!athena_box2d_joint_set_limits(joint, 5.0f, 0.5f), "%s lower > upper", name);
			CHECK(!athena_box2d_joint_set_limits(joint, 0.0f, INFINITY), "%s infinite limit", name);
			break;
		default:
			CHECK(!athena_box2d_joint_set_limits(joint, 0.0f, 1.0f), "%s has no limits", name);
			break;
		}

		step(world, 2);
		b2DestroyJoint(joint, false);
	}

	CHECK(athena_box2d_revolute_limits_valid(-0.99f * B2_PI, 0.99f * B2_PI), "widest revolute limits");
	CHECK(!athena_box2d_revolute_limits_valid(-B2_PI, 0.0f), "revolute lower limit of -pi");
	CHECK(!athena_box2d_revolute_limits_valid(0.0f, NAN), "NaN revolute limit");
	b2DestroyWorld(world);
}

static void test_valid_float(void)
{
	CHECK(athena_box2d_valid_float(0.0f) && athena_box2d_valid_float(-1e30f) && athena_box2d_valid_float(1.7e38f),
		"ordinary floats");
	CHECK(!athena_box2d_valid_float(INFINITY) && !athena_box2d_valid_float(-INFINITY), "infinities");
	CHECK(!athena_box2d_valid_float(NAN), "NaN");
	/* 2^127 and up: the EE turns infinities into such numbers. */
	CHECK(!athena_box2d_valid_float(FLT_MAX) && !athena_box2d_valid_float(1.8e38f), "beyond 2^127");
	CHECK(athena_box2d_max_coordinate() == B2_HUGE, "max coordinate");
}

static void test_memory_budget(void)
{
	b2WorldId world = new_world(b2Vec2_zero);
	size_t used = athena_box2d_memory_used();

	CHECK(used > 0, "Box2D memory in use: %zu", used);
	CHECK(athena_box2d_memory_limit() == 0 && athena_box2d_memory_available(), "no limit by default");
	athena_box2d_set_memory_limit(used);
	CHECK(!athena_box2d_memory_available(), "limit reached");
	athena_box2d_set_memory_limit(used + 1024 * 1024);
	CHECK(athena_box2d_memory_available(), "below the limit");
	athena_box2d_set_memory_limit(0);
	b2DestroyWorld(world);
}

static void test_snapshots(void)
{
	b2WorldId world = new_world((b2Vec2){ 0, -10 });
	b2BodyId body = new_body(world, b2_dynamicBody, (b2Vec2){ 0, 5 }, 0.0f);
	int size = 0;
	uint8_t *image = athena_box2d_snapshot(world, &size);

	CHECK(image && size > 8, "snapshot of %d bytes", size);
	if (!image) {
		b2DestroyWorld(world);
		return;
	}
	step(world, 30);
	CHECK(b2Body_GetPosition(body).y < 5.0f, "body fell");
	CHECK(athena_box2d_restore(world, image, size) == ATHENA_BOX2D_RESTORED, "restored");
	CHECK(b2Body_IsValid(body) && b2Body_GetPosition(body).y == 5.0f, "state back: %g", (double)b2Body_GetPosition(body).y);

	image[size / 2] ^= 0x40;
	CHECK(athena_box2d_restore(world, image, size) == ATHENA_BOX2D_RESTORE_DAMAGED, "damaged payload");
	image[size / 2] ^= 0x40;
	CHECK(athena_box2d_restore(world, image, size - 1) == ATHENA_BOX2D_RESTORE_REJECTED, "truncated image");
	CHECK(athena_box2d_restore(world, image, 4) == ATHENA_BOX2D_RESTORE_REJECTED, "tiny image");
	CHECK(b2World_IsValid(world) && b2Body_IsValid(body), "rejected images leave the world intact");
	step(world, 2);
	free(image);
	b2DestroyWorld(world);
}

static void test_world_limit(void)
{
	b2WorldId worlds[B2_MAX_WORLDS];
	b2WorldId extra;

	for (int i = 0; i < B2_MAX_WORLDS; i++) {
		worlds[i] = new_world(b2Vec2_zero);
		CHECK(b2World_IsValid(worlds[i]), "world %d", i);
	}
	extra = new_world(b2Vec2_zero);
	CHECK(B2_IS_NULL(extra), "world %d is refused with a null id", B2_MAX_WORLDS);
	b2DestroyWorld(worlds[0]);
	extra = new_world(b2Vec2_zero);
	CHECK(b2World_IsValid(extra), "a destroyed world frees its slot");
	CHECK(!b2World_IsValid(worlds[0]), "the old id of a reused slot stays invalid");
	b2DestroyWorld(extra);
	for (int i = 1; i < B2_MAX_WORLDS; i++)
		b2DestroyWorld(worlds[i]);
}

int main(void)
{
	b2SetAssertFcn(count_assertion);

	test_names();
	test_geometry();
	test_shapes_in_world();
	test_frames_angle();
	test_frames_axis();
	test_joint_params();
	test_valid_float();
	test_memory_budget();
	test_snapshots();
	test_world_limit();

	CHECK(assertions == 0, "%d Box2D assertions", assertions);
	printf("box2d: %d checks, %d failures\n", checks, failures);
	return failures != 0;
}
