/* Sonic 3 family mods: Knuckles & Knuckles. Character mods are mutually
 * exclusive, so enabling one refuses while any other feature in the
 * "Characters" group (here or in the composed base provider) is enabled. */
#include "sonic3_mods.h"
#include "sonic3_knuckles_army.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static char s_path[1024], s_error[256];

S3ArmyConfig s3_army;
void s3_army_defaults(void) { s3_army.enabled = 0; s3_army.size = S3_ARMY_DEFAULT; }
int s3_army_valid_size(unsigned size) { return size >= S3_ARMY_MIN && size <= S3_ARMY_MAX; }

/* Both the UI and hand-edited INI use the same bounded decimal parser.
 * Reject signs, suffixes and overflow without changing the current value. */
static int parse_size(const char *text, unsigned *result)
{
    unsigned size = 0;
    if (!text || !*text) return 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return 0;
        size = size * 10 + (unsigned)(*text - '0');
        if (size > S3_ARMY_MAX) return 0;
    }
    if (!s3_army_valid_size(size)) return 0;
    *result = size;
    return 1;
}

void s3_mods_load(const char *settings_path, const char *mode)
{
    s3_army_defaults();
    snprintf(s_path, sizeof s_path, "%s", settings_path ? settings_path : "settings.ini");
    char *slash = strrchr(s_path, '/'), *backslash = strrchr(s_path, '\\');
    char *base = slash && (!backslash || slash > backslash) ? slash + 1 : backslash ? backslash + 1 : s_path;
    snprintf(base, sizeof s_path - (size_t)(base - s_path), "%s-mods.ini", mode);
    FILE *file = fopen(s_path, "rb");
    if (!file) return;
    char line[256], section[64] = "";
    while (fgets(line, sizeof line, file)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') { snprintf(section, sizeof section, "%s", line); continue; }
        char *value = strchr(line, '=');
        if (!value || strcmp(section, "[knuckles-army]")) continue;
        *value++ = 0;
        if (!strcmp(line, "enabled")) s3_army.enabled = atoi(value) == 1;
        else if (!strcmp(line, "size")) {
            unsigned size;
            if (parse_size(value, &size)) s3_army.size = size;
        }
    }
    fclose(file);
}

int s3_mods_save(void)
{
    char temporary[1040];
    snprintf(temporary, sizeof temporary, "%s.tmp", s_path);
    FILE *file = fopen(temporary, "wb");
    if (!file) { snprintf(s_error, sizeof s_error, "Unable to write %s", s_path); return 0; }
    fprintf(file, "[knuckles-army]\nenabled=%d\nsize=%u\n", s3_army.enabled, s3_army.size);
    int ok = !ferror(file);
    if (fclose(file)) ok = 0;
#ifdef _WIN32
    if (ok) ok = MoveFileExA(temporary, s_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (ok) ok = rename(temporary, s_path) == 0;
#endif
    if (!ok) { snprintf(s_error, sizeof s_error, "Unable to save %s", s_path); return 0; }
    return 1;
}

int s3_mods_netplay_allowed(void) { return !s3_army.enabled; }

#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
#define COPY(dst, value) snprintf(dst, sizeof(dst), "%s", value)
static const RecompLauncherCModProvider *base;
static const char PACKAGE[] = "sonic3.knuckles-army", FEATURE[] = "knuckles-army", GROUP[] = "Characters";
static const char DESCRIPTION[] =
    "Pick Knuckles and a crowd of Knuckles comes along. Each extra follows you "
    "with native Knuckles physics; a controller on player 2, 3 or 4 takes over "
    "the matching extra. Lost or fallen extras glide back in. Local play only.";

static int mine(const char *p, const char *f) { return p && !strcmp(p, PACKAGE) && (!f || !strcmp(f, FEATURE)); }
static int base_count(int (*fn)(void *)) { return base && fn ? fn(base->ctx) : 0; }
static const char *status(void)
{
    return s3_army.enabled ? "Enabled: choose Knuckles on Data Select (or the title on S&K)" : "Disabled";
}
static int package_count(void *ctx) { (void)ctx; return 1 + base_count(base ? base->package_count : NULL); }
static int package_get(void *ctx, int i, RecompLauncherCModPackage *out)
{
    (void)ctx;
    if (i >= 1) return base && base->package_get && base->package_get(base->ctx, i - 1, out);
    if (i < 0 || !out) return 0;
    memset(out, 0, sizeof *out);
    COPY(out->id, PACKAGE); COPY(out->name, "Knuckles & Knuckles"); COPY(out->version, "0.1.0");
    COPY(out->author, "Sonic3AndKnucklesRecomp contributors");
    COPY(out->description, DESCRIPTION); COPY(out->license, "Project license; uses only your ROM");
    COPY(out->status, status());
    out->enabled = s3_army.enabled;
    return 1;
}
static int feature_count(void *ctx) { (void)ctx; return 1 + base_count(base ? base->feature_count : NULL); }
static int feature_get(void *ctx, int i, RecompLauncherCModFeature *out)
{
    if (i >= 1) return base && base->feature_get && base->feature_get(base->ctx, i - 1, out);
    RecompLauncherCModPackage p;
    if (!out || i < 0 || !package_get(ctx, 0, &p)) return 0;
    memset(out, 0, sizeof *out);
    COPY(out->id, FEATURE); COPY(out->package_id, p.id); COPY(out->package_name, p.name);
    COPY(out->package_version, p.version); COPY(out->name, "Knuckles & Knuckles");
    COPY(out->author, p.author); COPY(out->description, DESCRIPTION); COPY(out->group, GROUP);
    COPY(out->status, status());
    out->enabled = s3_army.enabled; out->option_count = 1;
    return 1;
}
/* Another enabled character mod, in this provider or the composed base. */
static const char *character_conflict(void)
{
    static RecompLauncherCModFeature f;
    int n = base_count(base ? base->feature_count : NULL);
    for (int i = 0; i < n; ++i)
        if (base->feature_get(base->ctx, i, &f) && f.enabled && !strcmp(f.group, GROUP)) return f.name;
    return NULL;
}
static int feature_enable(void *ctx, const char *p, const char *f, int on)
{
    (void)ctx; s_error[0] = 0;
    if (!mine(p, f)) return !mine(p, NULL) && base && base->feature_enable && base->feature_enable(base->ctx, p, f, on);
    const char *other = on ? character_conflict() : NULL;
    if (other) { snprintf(s_error, sizeof s_error, "Disable %s first: character mods are exclusive", other); return 0; }
    s3_army.enabled = on != 0;
    return 1;
}
static int set_enabled(void *ctx, const char *p, int on)
{
    if (mine(p, NULL)) return feature_enable(ctx, p, FEATURE, on);
    return base && base->set_enabled && base->set_enabled(base->ctx, p, on);
}
static int option_get(void *ctx, const char *p, const char *f, int n, RecompLauncherCModOption *out)
{
    (void)ctx;
    if (!mine(p, f)) return !mine(p, NULL) && base && base->feature_option_get && base->feature_option_get(base->ctx, p, f, n, out);
    if (n || !out) return 0;
    memset(out, 0, sizeof *out);
    COPY(out->id, "crowd"); COPY(out->label, "Extra Knuckles");
    snprintf(out->description, sizeof out->description,
        "1-%u extra Knuckles, plus your character. Default: %u. "
        "Busy stages may temporarily use fewer extras to keep room for level objects.",
        S3_ARMY_MAX, S3_ARMY_DEFAULT);
    snprintf(out->value, sizeof out->value, "%u", s3_army.size);
    snprintf(out->default_value, sizeof out->default_value, "%u", S3_ARMY_DEFAULT);
    out->type = RECOMP_MOD_OPTION_INTEGER; out->step = 1;
    out->min_value = S3_ARMY_MIN; out->max_value = S3_ARMY_MAX;
    return 1;
}
static int choice_get(void *ctx, const char *p, const char *f, const char *o, int n, RecompLauncherCModChoice *out)
{
    (void)ctx;
    if (!mine(p, f)) return !mine(p, NULL) && base && base->feature_choice_get && base->feature_choice_get(base->ctx, p, f, o, n, out);
    return 0;
}
static int set_option(void *ctx, const char *p, const char *f, const char *o, const char *v)
{
    (void)ctx; s_error[0] = 0;
    if (!mine(p, f)) return !mine(p, NULL) && base && base->feature_set_option && base->feature_set_option(base->ctx, p, f, o, v);
    unsigned size;
    if (!o || strcmp(o, "crowd")) return 0;
    if (!parse_size(v, &size)) {
        snprintf(s_error, sizeof s_error, "Enter a whole number from %u to %u for Extra Knuckles.",
                 S3_ARMY_MIN, S3_ARMY_MAX);
        return 0;
    }
    s3_army.size = size;
    return 1;
}
static int commit(void *ctx, const char *image)
{
    (void)ctx; s_error[0] = 0;
    const char *other = s3_army.enabled ? character_conflict() : NULL;
    if (other) { snprintf(s_error, sizeof s_error, "Knuckles & Knuckles cannot combine with %s", other); return 0; }
    if (base && base->commit && !base->commit(base->ctx, image)) {
        COPY(s_error, base->last_error ? base->last_error(base->ctx) : "Unable to save common mods");
        return 0;
    }
    return s3_mods_save();
}
static int commit_netplay(void *ctx, const char *image)
{
    (void)ctx; s_error[0] = 0;
    if (s3_army.enabled) { COPY(s_error, "Disable the local-only Knuckles & Knuckles mod before netplay"); return 0; }
    return !base || !base->commit_netplay || base->commit_netplay(base->ctx, image);
}
static const char *last_error(void *ctx)
{
    (void)ctx;
    return *s_error ? s_error : base && base->last_error ? base->last_error(base->ctx) : "";
}
static int resource_count(void *ctx, const char *p, const char *f)
{
    (void)ctx;
    return !mine(p, NULL) && base && base->feature_resource_count ? base->feature_resource_count(base->ctx, p, f) : 0;
}
static int resource_get(void *ctx, const char *p, const char *f, int n, RecompLauncherCModResource *out)
{
    (void)ctx;
    return !mine(p, NULL) && base && base->feature_resource_get && base->feature_resource_get(base->ctx, p, f, n, out);
}
static int resource_set(void *ctx, const char *p, const char *f, const char *r, const char *path)
{
    (void)ctx;
    return !mine(p, NULL) && base && base->feature_resource_set_path && base->feature_resource_set_path(base->ctx, p, f, r, path);
}
const RecompLauncherCModProvider *s3_mods(const RecompLauncherCModProvider *common)
{
    static RecompLauncherCModProvider p;
    base = common; memset(&p, 0, sizeof p);
    p.package_count = package_count; p.package_get = package_get; p.set_enabled = set_enabled;
    p.feature_count = feature_count; p.feature_get = feature_get; p.feature_enable = feature_enable;
    p.feature_option_get = option_get; p.feature_choice_get = choice_get; p.feature_set_option = set_option;
    p.feature_resource_count = resource_count; p.feature_resource_get = resource_get;
    p.feature_resource_set_path = resource_set;
    p.commit = commit; p.commit_netplay = commit_netplay; p.last_error = last_error;
    p.archive_extension = common ? common->archive_extension : ".genmod";
    p.archive_description = common ? common->archive_description : "GenesisRecomp mod package";
    return &p;
}
#else
const struct RecompLauncherCModProvider *s3_mods(const struct RecompLauncherCModProvider *common) { return common; }
#endif
