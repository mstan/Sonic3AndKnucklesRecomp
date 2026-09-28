#include "sonic3_mods.h"
#include "sonic3_knuckles_army.h"
#include "recomp_launcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static const char *package = "sonic3.knuckles-army", *feature = "knuckles-army";

int main(void)
{
    remove("test-knuckles-mods.ini");
    s3_mods_load("settings.ini", "test-knuckles");
    CHECK(!s3_army.enabled && s3_army.size == 16);
    const RecompLauncherCModProvider *p = s3_mods(NULL);
    RecompLauncherCModOption option;
    CHECK(p->feature_option_get(NULL, package, feature, 0, &option));
    CHECK(option.type == RECOMP_MOD_OPTION_INTEGER && option.step == 1);
    CHECK(option.min_value == 1 && option.max_value == S3_ARMY_MAX);
    CHECK(!strcmp(option.default_value, "16") && option.choice_count == 0);
    CHECK(p->feature_enable(NULL, package, feature, 1));
    CHECK(!s3_mods_netplay_allowed());
    CHECK(!p->commit_netplay(NULL, NULL));
    for (unsigned n = S3_ARMY_MIN; n <= S3_ARMY_MAX; ++n) {
        char value[32]; snprintf(value, sizeof value, "%u", n);
        CHECK(p->feature_set_option(NULL, package, feature, "crowd", value));
        CHECK(s3_army.size == n);
        CHECK(p->commit(NULL, NULL));
        s3_army_defaults();
        s3_mods_load("settings.ini", "test-knuckles");
        CHECK(s3_army.enabled && s3_army.size == n);
    }
    const char *invalid[] = { NULL, "", "0", "-1", "+16", "16x", "1.5", " 16", "16 ",
        "75", "100", "4294967312", "18446744073709551632", "9999999999999999999999999999" };
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        CHECK(!p->feature_set_option(NULL, package, feature, "crowd", invalid[i]));
        CHECK(s3_army.size == S3_ARMY_MAX && *p->last_error(NULL));
        FILE *f = fopen("test-knuckles-mods.ini", "wb"); CHECK(f);
        fprintf(f, "[knuckles-army]\nenabled=1\nsize=%s\n", invalid[i] ? invalid[i] : "");
        CHECK(!fclose(f));
        s3_mods_load("settings.ini", "test-knuckles");
        CHECK(s3_army.enabled && s3_army.size == S3_ARMY_DEFAULT);
        s3_army.size = S3_ARMY_MAX;
    }
    CHECK(p->feature_set_option(NULL, package, feature, "crowd", "17"));
    CHECK(!*p->last_error(NULL));
    CHECK(p->feature_enable(NULL, package, feature, 0));
    CHECK(s3_mods_netplay_allowed());
    remove("test-knuckles-mods.ini");
    puts("Numeric crowd range, persistence, invalid input and netplay isolation passed");
    return 0;
}
