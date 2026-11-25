#ifndef NEBEL_CORE_MANIFEST_H
#define NEBEL_CORE_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>

#define NEBEL_PATH_MAX 128

/* Rejects absolute paths, backslashes, drive prefixes, and '..' traversal segments. */
bool nebel_path_is_safe(const char *rel_path);

bool nebel_path_join_asset(const char *base_dir, const char *rel_path,
                           char *out, size_t out_cap);

/* Extracts validated safe relative paths from JSON array(s) in manifest.json. */
int nebel_manifest_parse(const char *json, char paths[][NEBEL_PATH_MAX], int max_paths);

#endif /* NEBEL_CORE_MANIFEST_H */
