#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "level.h"

static const char *level_file(const char *name) {
    static char buf[1024];
    const char *srcdir = getenv("srcdir");
    snprintf(buf, sizeof buf, "%s/levels/%s", srcdir ? srcdir : ".", name);
    return buf;
}

static level a, b;   /* big: keep them off the stack */
static char err[256];

START_TEST(test_arena_file_matches_builtin)
{
    ck_assert_msg(level_load(&a, level_file("arena.lvl"), err, sizeof err) == 0, "%s", err);
    level_builtin_arena(&b);
    ck_assert_str_eq(a.name, b.name);
    ck_assert(memcmp(&a, &b, sizeof a) == 0);
}
END_TEST

/* Every level shipped in levels/ loads, and nothing starts inside a wall. */
static const char *const shipped[] = {
    "arena.lvl", "bunker.lvl", "colonnade.lvl", "crossroads.lvl", "maze.lvl", "warehouse.lvl",
};

static int inside_box(const level *lv, float x, float z) {
    for (int i = 0; i < lv->n_boxes; i++)
        if (x > lv->boxes[i].min.x && x < lv->boxes[i].max.x &&
            z > lv->boxes[i].min.z && z < lv->boxes[i].max.z) return 1;
    return 0;
}

START_TEST(test_shipped_levels_load)
{
    const char *f = shipped[_i];
    ck_assert_msg(level_load(&a, level_file(f), err, sizeof err) == 0, "%s", err);
    ck_assert_msg(a.name[0] != 0, "%s: no name", f);
    ck_assert_msg(!inside_box(&a, a.player.x, a.player.z), "%s: player starts in a wall", f);
    for (int i = 0; i < a.n_spawns; i++)
        ck_assert_msg(!inside_box(&a, a.spawns[i].x, a.spawns[i].z), "%s: spawn %d in a wall", f, i);
    for (int i = 0; i < a.n_boxes; i++) {
        ck_assert_float_ge(a.boxes[i].min.x, -a.half_x);
        ck_assert_float_le(a.boxes[i].max.x, a.half_x);
        ck_assert_float_ge(a.boxes[i].min.z, -a.half_z);
        ck_assert_float_le(a.boxes[i].max.z, a.half_z);
    }
}
END_TEST

START_TEST(test_warehouse_loads)
{
    ck_assert_msg(level_load(&a, level_file("warehouse.lvl"), err, sizeof err) == 0, "%s", err);
    ck_assert_str_eq(a.name, "WAREHOUSE");
    ck_assert_float_eq(a.half_x, 22.0f);          /* 22 inner columns x 2 m */
    ck_assert_float_eq(a.half_z, 16.0f);          /* 16 inner rows x 2 m */
    ck_assert_int_eq(a.n_spawns, 4);
    ck_assert_int_eq(a.n_pillars, 3);
    ck_assert_int_gt(a.n_boxes, 10);
    ck_assert_int_lt(a.n_boxes, 60);              /* runs were merged */
    for (int i = 0; i < a.n_boxes; i++) {         /* everything inside the walls */
        ck_assert_float_ge(a.boxes[i].min.x, -a.half_x);
        ck_assert_float_le(a.boxes[i].max.x, a.half_x);
        ck_assert_float_ge(a.boxes[i].min.z, -a.half_z);
        ck_assert_float_le(a.boxes[i].max.z, a.half_z);
    }
}
END_TEST

START_TEST(test_map_coordinates_and_border)
{
    const char *txt =
        "tile 2\n"
        "map\n"
        "#####\n"
        "#P..#\n"
        "#..E#\n"
        "#####\n"
        "end\n";
    ck_assert_msg(level_parse(&a, txt, "t", err, sizeof err) == 0, "%s", err);
    ck_assert_float_eq(a.half_x, 3.0f);           /* border dropped: 3 x 2 cells */
    ck_assert_float_eq(a.half_z, 2.0f);
    ck_assert_int_eq(a.n_boxes, 0);
    ck_assert_float_eq(a.player.x, -2.0f);        /* top-left cell centre */
    ck_assert_float_eq(a.player.z, 1.0f);         /* top row is +z */
    ck_assert_int_eq(a.n_spawns, 1);
    ck_assert_float_eq(a.spawns[0].x, 2.0f);
    ck_assert_float_eq(a.spawns[0].z, -1.0f);
}
END_TEST

START_TEST(test_map_merges_runs)
{
    const char *txt =
        "tile 1\n"
        "wall_height 3\n"
        "crate_height 0.5\n"
        "map\n"
        "P.........\n"
        "..###.....\n"
        "..###..==.\n"
        "..#.......\n"
        "..........\n"
        "end\n";
    ck_assert_msg(level_parse(&a, txt, "t", err, sizeof err) == 0, "%s", err);
    ck_assert_int_eq(a.n_boxes, 3);   /* 3x2 block + 1x1 below it + the crates */
    /* The 3x2 wall block: columns 2..4, rows 1..2 of a 10 x 5 map. */
    ck_assert_float_eq(a.boxes[0].min.x, -3.0f);
    ck_assert_float_eq(a.boxes[0].max.x, 0.0f);
    ck_assert_float_eq(a.boxes[0].min.z, -0.5f);
    ck_assert_float_eq(a.boxes[0].max.z, 1.5f);
    ck_assert_float_eq(a.boxes[0].max.y, 3.0f);
    int crates = 0;
    for (int i = 0; i < a.n_boxes; i++) crates += a.boxes[i].max.y == 0.5f;
    ck_assert_int_eq(crates, 1);
}
END_TEST

START_TEST(test_commands)
{
    const char *txt =
        "# comment\n"
        "name  My Level   \n"
        "size 20 10\n"
        "player 1 2 90\n"
        "pillar 3 3 0.5   # trailing comment\n"
        "box -5 -2 -3 2 1.5\n"
        "spawn 8 4\n";
    ck_assert_msg(level_parse(&a, txt, "t", err, sizeof err) == 0, "%s", err);
    ck_assert_str_eq(a.name, "MY LEVEL");
    ck_assert_float_eq(a.half_x, 10.0f);
    ck_assert_float_eq(a.half_z, 5.0f);
    ck_assert_float_eq_tol(a.player_yaw, 1.5707963f, 1e-6f);
    ck_assert_int_eq(a.n_pillars, 1);
    ck_assert_int_eq(a.n_boxes, 1);
    ck_assert_float_eq(a.boxes[0].max.y, 1.5f);
    ck_assert_int_eq(a.n_spawns, 1);
}
END_TEST

START_TEST(test_errors_name_the_line)
{
    ck_assert_int_eq(level_parse(&a, "size 20 20\npilar 1 1 1\n", "x.lvl", err, sizeof err), -1);
    ck_assert_str_eq(err, "x.lvl:2: unknown command 'pilar'");
    ck_assert_int_eq(level_parse(&a, "box 1 2 3\n", "x.lvl", err, sizeof err), -1);
    ck_assert_str_eq(err, "x.lvl:1: 'box' takes 5 numbers, got 3");
    ck_assert_int_eq(level_parse(&a, "pillar 1 one 1\n", "x.lvl", err, sizeof err), -1);
    ck_assert_str_eq(err, "x.lvl:1: bad numbers after 'pillar'");
    ck_assert_int_eq(level_parse(&a, "map\nP..\n.X.\nend\n", "x.lvl", err, sizeof err), -1);
    ck_assert_str_eq(err, "x.lvl:3: unknown map character 'X' (column 2)");
    ck_assert_int_eq(level_parse(&a, "map\nP..\n", "x.lvl", err, sizeof err), -1);
    ck_assert_str_eq(err, "x.lvl:2: map has no 'end'");
    ck_assert_int_eq(level_parse(&a, "player 50 0\n", "x.lvl", err, sizeof err), -1);
    ck_assert_str_eq(err, "x.lvl: player start (50.0, 0.0) is outside the walls");
    ck_assert_int_eq(level_parse(&a, "box -1 -4 1 -2 2\n", "x.lvl", err, sizeof err), -1);
    ck_assert_str_eq(err, "x.lvl: player start (0.0, -3.0) is inside a box");
    ck_assert_int_eq(level_load(&a, "/nonexistent/x.lvl", err, sizeof err), -1);
    ck_assert_str_eq(err, "/nonexistent/x.lvl: cannot open");
}
END_TEST

START_TEST(test_clear_line)
{
    const char *txt = "box -1 -1 1 1 3\nbox 4 -1 5 1 0.8\n";
    ck_assert_msg(level_parse(&a, txt, "t", err, sizeof err) == 0, "%s", err);
    vector l = {-5, 0, 0}, r = {3, 0, 0}, far = {8, 0, 0}, up = {-5, 0, 5};
    ck_assert_int_eq(level_clear_line(&a, l, r, 0.0f), 0);     /* through the wall */
    ck_assert_int_eq(level_clear_line(&a, l, up, 0.0f), 1);    /* beside it */
    ck_assert_int_eq(level_clear_line(&a, r, far, 0.0f), 0);   /* through the crate */
    ck_assert_int_eq(level_clear_line(&a, r, far, 1.4f), 1);   /* ...seen over it */
    level_builtin_arena(&b);                                   /* no boxes at all */
    ck_assert_int_eq(level_clear_line(&b, l, far, 0.0f), 1);
}
END_TEST

int main(void) {
    Suite *s = suite_create("level");
    TCase *tc = tcase_create("Format");
    tcase_add_test(tc, test_arena_file_matches_builtin);
    tcase_add_test(tc, test_warehouse_loads);
    tcase_add_loop_test(tc, test_shipped_levels_load, 0, (int)(sizeof shipped / sizeof shipped[0]));
    tcase_add_test(tc, test_map_coordinates_and_border);
    tcase_add_test(tc, test_map_merges_runs);
    tcase_add_test(tc, test_commands);
    tcase_add_test(tc, test_errors_name_the_line);
    tcase_add_test(tc, test_clear_line);
    suite_add_tcase(s, tc);
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
