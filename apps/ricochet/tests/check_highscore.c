#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "highscore.h"

static hs_entry entry(int score) {
    hs_entry e = {score, 3, 9, 50, 30, 40, 1, -1, 1700000000LL};
    return e;
}

/* A fresh file path in the build directory, unique per test. */
static const char *tmp_path(const char *name) {
    static char buf[256];
    snprintf(buf, sizeof buf, "hs_test_%s_%d.txt", name, (int)getpid());
    remove(buf);
    return buf;
}

START_TEST(test_insert_sorted_and_capped)
{
    hs_table t;
    hs_clear(&t);
    int scores[] = {500, 2000, 100, 1500, 900, 3000, 50, 700, 1200, 800, 400, 2500};
    for (int i = 0; i < 12; i++) hs_insert(&t, &(hs_entry){.score = scores[i]});
    ck_assert_int_eq(t.n, HS_MAX);
    for (int i = 1; i < t.n; i++) ck_assert_int_ge(t.e[i - 1].score, t.e[i].score);
    ck_assert_int_eq(t.e[0].score, 3000);
    ck_assert_int_eq(t.e[HS_MAX - 1].score, 400);   /* 100 and 50 fell off */
}
END_TEST

START_TEST(test_rank_ties_and_rejects)
{
    hs_table t;
    hs_clear(&t);
    hs_entry a = entry(1000), b = entry(1000);
    a.kills = 1; b.kills = 2;
    ck_assert_int_eq(hs_insert(&t, &a), 0);
    ck_assert_int_eq(hs_insert(&t, &b), 1);       /* tie: earlier game stays first */
    ck_assert_int_eq(t.e[0].kills, 1);
    ck_assert_int_eq(hs_rank(&t, 0), -1);         /* zero never ranks */
    ck_assert_int_eq(hs_insert(&t, &(hs_entry){.score = -5}), -1);
    for (int i = 0; i < HS_MAX; i++) hs_insert(&t, &(hs_entry){.score = 5000});
    ck_assert_int_eq(hs_rank(&t, 4999), -1);      /* full table, too low */
    ck_assert_int_eq(hs_rank(&t, 5001), 0);
}
END_TEST

START_TEST(test_save_load_roundtrip)
{
    const char *p = tmp_path("roundtrip");
    hs_table t, u;
    hs_clear(&t);
    for (int i = 1; i <= 4; i++) {
        hs_entry e = entry(i * 1000);
        e.mag = 10 * i; e.start_wave = i; e.start_ammo = i == 2 ? 5 : -1;
        e.date = 1700000000LL + i;
        hs_insert(&t, &e);
    }
    ck_assert_int_eq(hs_save(&t, p), 0);
    ck_assert_int_eq(hs_load(&u, p), 0);
    ck_assert_int_eq(u.n, t.n);
    ck_assert(memcmp(t.e, u.e, (size_t)t.n * sizeof t.e[0]) == 0);
    remove(p);
}
END_TEST

START_TEST(test_missing_file_is_empty)
{
    hs_table t;
    t.n = 7;
    ck_assert_int_eq(hs_load(&t, "/nonexistent/dir/scores.txt"), 0);
    ck_assert_int_eq(t.n, 0);
}
END_TEST

START_TEST(test_damaged_file)
{
    const char *p = tmp_path("damaged");
    FILE *f = fopen(p, "w");
    fputs("ricochet-scores 1\n"
          "300 2 5 20 10 40 1 -1 1700000000\n"
          "garbage line\n"
          "900 4 12 60 40 40 1 -1 1700000001\n"
          "1 2 3\n"
          "600 3 8 30 20 40 1 -1 1700000002\n", f);
    fclose(f);
    hs_table t;
    ck_assert_int_eq(hs_load(&t, p), 0);
    ck_assert_int_eq(t.n, 3);                     /* bad lines skipped */
    ck_assert_int_eq(t.e[0].score, 900);          /* and re-sorted */
    ck_assert_int_eq(t.e[2].score, 300);

    f = fopen(p, "w");
    fputs("not a score file\n500 1 1 1 1 40 1 -1 0\n", f);
    fclose(f);
    ck_assert_int_eq(hs_load(&t, p), -1);
    ck_assert_int_eq(t.n, 0);
    remove(p);
}
END_TEST

START_TEST(test_save_replaces_and_cleans_up)
{
    const char *p = tmp_path("replace");
    hs_table t, u;
    hs_clear(&t);
    hs_insert(&t, &(hs_entry){.score = 100});
    ck_assert_int_eq(hs_save(&t, p), 0);
    hs_insert(&t, &(hs_entry){.score = 200});
    ck_assert_int_eq(hs_save(&t, p), 0);          /* over an existing file */
    hs_load(&u, p);
    ck_assert_int_eq(u.n, 2);
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    ck_assert_ptr_null(fopen(tmp, "r"));          /* no temp file left */
    ck_assert_int_eq(hs_save(&t, "/nonexistent/dir/scores.txt"), -1);
    remove(p);
}
END_TEST

int main(void) {
    Suite *s = suite_create("highscore");
    TCase *tc = tcase_create("Table");
    tcase_add_test(tc, test_insert_sorted_and_capped);
    tcase_add_test(tc, test_rank_ties_and_rejects);
    tcase_add_test(tc, test_save_load_roundtrip);
    tcase_add_test(tc, test_missing_file_is_empty);
    tcase_add_test(tc, test_damaged_file);
    tcase_add_test(tc, test_save_replaces_and_cleans_up);
    suite_add_tcase(s, tc);
    SRunner *sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
