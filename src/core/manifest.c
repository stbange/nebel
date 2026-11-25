#include "core/manifest.h"

#include <stdio.h>
#include <string.h>

bool nebel_path_is_safe(const char *rel_path) {
    if (!rel_path || rel_path[0] == '\0') return false;
    if (rel_path[0] == '/' || rel_path[0] == '\\') return false;

    const char *p = rel_path;
    while (*p) {
        const char *seg_start = p;
        while (*p && *p != '/') {
            if (*p == '\\' || *p == ':' || (unsigned char)*p < 0x20u) return false;
            p++;
        }
        size_t seg_len = (size_t)(p - seg_start);
        if (seg_len == 2 && seg_start[0] == '.' && seg_start[1] == '.') {
            return false;
        }
        if (*p == '/') p++;
    }
    return true;
}

bool nebel_path_join_asset(const char *base_dir, const char *rel_path,
                           char *out, size_t out_cap) {
    if (!out || out_cap == 0 || !nebel_path_is_safe(rel_path)) return false;
    if (!base_dir || base_dir[0] == '\0') {
        int n = snprintf(out, out_cap, "%s", rel_path);
        return n > 0 && (size_t)n < out_cap;
    }
    size_t blen = strlen(base_dir);
    bool has_slash = (base_dir[blen - 1] == '/' || base_dir[blen - 1] == '\\');
    int n = snprintf(out, out_cap, "%s%s%s", base_dir, has_slash ? "" : "/", rel_path);
    return n > 0 && (size_t)n < out_cap;
}

int nebel_manifest_parse(const char *json, char paths[][NEBEL_PATH_MAX], int max_paths) {
    if (!json || !paths || max_paths <= 0) return 0;

    /* Find "files" key, then parse all JSON strings inside its [...] array */
    const char *files = strstr(json, "\"files\"");
    if (!files) return 0;
    const char *p = strchr(files, '[');
    if (!p) return 0;
    p++;

    int count = 0;
    while (*p && *p != ']') {
        while (*p && *p != '"' && *p != ']') p++;
        if (*p != '"') break;
        p++;
        const char *start = p;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) p += 2;
            else p++;
        }
        if (*p != '"') break;
        size_t len = (size_t)(p - start);
        p++;
        if (len > 0 && len < NEBEL_PATH_MAX && count < max_paths) {
            char buf[NEBEL_PATH_MAX];
            memcpy(buf, start, len);
            buf[len] = '\0';
            if (nebel_path_is_safe(buf)) {
                memcpy(paths[count], buf, len + 1u);
                count++;
            }
        }
    }
    return count;
}
