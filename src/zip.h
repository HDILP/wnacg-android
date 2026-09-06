#ifndef WNACG_ZIP_H
#define WNACG_ZIP_H

/* Whole-archive (zip) download support.
 *
 * The site offers a "download-index-aid-<id>.html" page per comic that links a
 * pre-packaged .zip of all images (served from a separate download CDN). Two
 * lines exist:
 *   Server 1 — a Cloudflare Worker POST endpoint (CONFIG.WORKER_API on the
 *              page, e.g. https://d1.wcdn.date/api/generate-link) returns a
 *              time-limited, signed download URL. One-shot: the URL does NOT
 *              support Range (403 on ranged GET), so it is whole-file only.
 *   Server 2 — a direct link (e.g. https://dl1.wn01.download/down/<key>.zip)
 *              that DOES support Range / resume.
 * Both are keyed by the page's CONFIG.FILE_KEY ("down/<dir>/<hash>.zip").
 */

typedef struct {
    char *worker_api;   /* CONFIG.WORKER_API URL (may be NULL) */
    char *file_key;     /* CONFIG.FILE_KEY  e.g. down/3827/<hash>.zip */
    char *file_name;    /* CONFIG.FILE_NAME (HTML entities &nbsp; decoded) */
    char *server2;      /* absolute https:// URL of the backup direct link */
} zip_download;

/* Parse a /download-index-aid-<id>.html page.
 * Returns 0 on success (fields malloc'd, caller frees with free_zip_download),
 * -1 on failure. */
int parse_zip_download(const char *html, zip_download *out);
void free_zip_download(zip_download *z);

/* Ask the Worker API for a signed Server-1 download URL.
 * api_url / file_key / file_name come from the parsed page. referer = the site
 * page URL (some workers check it). Returns malloc'd URL or NULL on failure. */
char *zip_generate_link(const char *api_url, const char *file_key,
                        const char *file_name, const char *referer);

/* Download a whole-archive zip to out_path.
 * server: 1 = signed Server-1 URL (generated via the Worker API), 2 = direct
 * Server-2 link. Prints progress-ish status lines to stdout/stderr.
 * Returns 0 on success. */
int cmd_zip(int argc, char **argv);      /* zip <id> [1|2] [outdir] */
int cmd_ziplink(int argc, char **argv);  /* ziplink <id> -> prints:
                                          *   ZIP1 <signed url>
                                          *   ZIP2 <direct url>   */

#endif /* WNACG_ZIP_H */
