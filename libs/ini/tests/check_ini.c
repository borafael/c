#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <check.h>
#include "ini.h"

/* Path to the test fixture, set at runtime via SRCDIR env var or hardcoded fallback. */
static const char *fixture_path(void)
{
    const char *srcdir = getenv("srcdir");
    /* build a static buffer; fine for tests */
    static char buf[4096];
    if (srcdir)
        snprintf(buf, sizeof(buf), "%s/tests/test.ini", srcdir);
    else
        snprintf(buf, sizeof(buf), "tests/test.ini");
    return buf;
}

/* --- test_load_nonexistent --- */

START_TEST(test_load_nonexistent)
{
    ini_file *ini = ini_load("/nonexistent/path/does/not/exist.ini");
    ck_assert_ptr_null(ini);
}
END_TEST

/* --- test_load_and_get --- */

START_TEST(test_load_and_get)
{
    ini_file *ini = ini_load(fixture_path());
    ck_assert_ptr_nonnull(ini);
    const char *v = ini_get(ini, "section", "key");
    ck_assert_ptr_nonnull(v);
    ck_assert_str_eq(v, "value");
    ini_free(ini);
}
END_TEST

/* --- test_get_int --- */

START_TEST(test_get_int)
{
    ini_file *ini = ini_load(fixture_path());
    ck_assert_ptr_nonnull(ini);
    int n = ini_get_int(ini, "section", "number", 0);
    ck_assert_int_eq(n, 42);
    int fallback = ini_get_int(ini, "section", "no_such_key", 99);
    ck_assert_int_eq(fallback, 99);
    ini_free(ini);
}
END_TEST

/* --- test_get_float --- */

START_TEST(test_get_float)
{
    ini_file *ini = ini_load(fixture_path());
    ck_assert_ptr_nonnull(ini);
    float f = ini_get_float(ini, "section", "decimal", 0.0f);
    ck_assert_float_eq_tol(f, 3.14f, 1e-4f);
    ini_free(ini);
}
END_TEST

/* --- test_get_bool --- */

START_TEST(test_get_bool)
{
    ini_file *ini = ini_load(fixture_path());
    ck_assert_ptr_nonnull(ini);
    int t = ini_get_bool(ini, "section", "enabled", 0);
    ck_assert_int_eq(t, 1);
    int f = ini_get_bool(ini, "section", "disabled", 1);
    ck_assert_int_eq(f, 0);
    ini_free(ini);
}
END_TEST

/* --- test_dot_separated_sections --- */

START_TEST(test_dot_separated_sections)
{
    ini_file *ini = ini_load(fixture_path());
    ck_assert_ptr_nonnull(ini);
    const char *v = ini_get(ini, "visual.animation.idle", "columns");
    ck_assert_ptr_nonnull(v);
    ck_assert_str_eq(v, "0");
    const char *loop = ini_get(ini, "visual.animation.idle", "loop");
    ck_assert_ptr_nonnull(loop);
    ck_assert_str_eq(loop, "true");
    ini_free(ini);
}
END_TEST

/* --- test_empty_section --- */

START_TEST(test_empty_section)
{
    ini_file *ini = ini_load(fixture_path());
    ck_assert_ptr_nonnull(ini);
    int kc = ini_key_count(ini, "selection");
    ck_assert_int_eq(kc, 0);

    /* The section must still be discoverable via iteration */
    int found = 0;
    int sc = ini_section_count(ini);
    for (int i = 0; i < sc; i++) {
        const char *name = ini_section_name(ini, i);
        if (name && strcmp(name, "selection") == 0) {
            found = 1;
            break;
        }
    }
    ck_assert_int_eq(found, 1);
    ini_free(ini);
}
END_TEST

/* --- test_comments_ignored --- */

START_TEST(test_comments_ignored)
{
    ini_file *ini = ini_load(fixture_path());
    ck_assert_ptr_nonnull(ini);
    const char *v = ini_get(ini, "section", "commented");
    ck_assert_ptr_null(v);
    ini_free(ini);
}
END_TEST

/* --- test_section_iteration --- */

START_TEST(test_section_iteration)
{
    ini_file *ini = ini_load(fixture_path());
    ck_assert_ptr_nonnull(ini);
    /* test.ini has: global + section + visual.animation.idle +
       visual.animation.walk + selection + locomotion = 6 sections */
    ck_assert_int_ge(ini_section_count(ini), 3);
    ini_free(ini);
}
END_TEST

/* --- test_key_iteration --- */

START_TEST(test_key_iteration)
{
    ini_file *ini = ini_load(fixture_path());
    ck_assert_ptr_nonnull(ini);
    /* section has: key, number, decimal, enabled, disabled = 5 keys */
    ck_assert_int_ge(ini_key_count(ini, "section"), 4);
    const char *kn = ini_key_name(ini, "section", 0);
    ck_assert_ptr_nonnull(kn);
    ini_free(ini);
}
END_TEST

/* --- test_global_keys --- */

START_TEST(test_global_keys)
{
    ini_file *ini = ini_load(fixture_path());
    ck_assert_ptr_nonnull(ini);
    const char *v = ini_get(ini, "", "global_key");
    ck_assert_ptr_nonnull(v);
    ck_assert_str_eq(v, "global_value");
    ini_free(ini);
}
END_TEST

/* --- suite --- */

Suite *ini_suite(void)
{
    Suite *s = suite_create("Ini");

    TCase *tc_load = tcase_create("Load");
    tcase_add_test(tc_load, test_load_nonexistent);
    tcase_add_test(tc_load, test_load_and_get);
    suite_add_tcase(s, tc_load);

    TCase *tc_get = tcase_create("Get");
    tcase_add_test(tc_get, test_get_int);
    tcase_add_test(tc_get, test_get_float);
    tcase_add_test(tc_get, test_get_bool);
    tcase_add_test(tc_get, test_dot_separated_sections);
    tcase_add_test(tc_get, test_empty_section);
    tcase_add_test(tc_get, test_comments_ignored);
    tcase_add_test(tc_get, test_global_keys);
    suite_add_tcase(s, tc_get);

    TCase *tc_iter = tcase_create("Iteration");
    tcase_add_test(tc_iter, test_section_iteration);
    tcase_add_test(tc_iter, test_key_iteration);
    suite_add_tcase(s, tc_iter);

    return s;
}

int main(void)
{
    SRunner *sr = srunner_create(ini_suite());
    srunner_run_all(sr, CK_NORMAL);
    int nf = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
