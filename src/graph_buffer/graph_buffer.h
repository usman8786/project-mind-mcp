/*
 * graph_buffer.h — In-memory graph buffer for pipeline indexing.
 *
 * Holds all nodes and edges in RAM during indexing, then dumps to SQLite.
 * Provides O(1) node lookup by qualified name and edge dedup by key.
 *
 * Depends on: foundation (hash_table, dyn_array), store (data structs)
 */
#ifndef PMM_GRAPH_BUFFER_H
#define PMM_GRAPH_BUFFER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

/* ── Opaque handle ──────────────────────────────────────────────── */

typedef struct pmm_gbuf pmm_gbuf_t;

/* Forward declare store for dump path */
typedef struct pmm_store pmm_store_t;

/* ── Node / Edge structs (owned by the buffer) ───────────────────── */

typedef struct {
    int64_t id;           /* temp ID (sequential from 1) */
    char *label;          /* heap-owned */
    char *name;           /* heap-owned */
    char *qualified_name; /* heap-owned */
    char *file_path;      /* heap-owned */
    int start_line;
    int end_line;
    char *properties_json; /* heap-owned JSON string, "{}" default */
} pmm_gbuf_node_t;

typedef struct {
    int64_t id;            /* temp ID */
    int64_t source_id;     /* temp node ID */
    int64_t target_id;     /* temp node ID */
    char *type;            /* heap-owned */
    char *properties_json; /* heap-owned JSON string, "{}" default */
} pmm_gbuf_edge_t;

/* ── Lifecycle ──────────────────────────────────────────────────── */

/* Create a new graph buffer for a project. */
pmm_gbuf_t *pmm_gbuf_new(const char *project, const char *root_path);

/* Create a graph buffer with a shared atomic ID source.
 * IDs are allocated via atomic_fetch_add on *id_source.
 * Used for parallel extraction where multiple gbufs need unique IDs.
 * If id_source is NULL, behaves like pmm_gbuf_new(). */
pmm_gbuf_t *pmm_gbuf_new_shared_ids(const char *project, const char *root_path,
                                    _Atomic int64_t *id_source);

/* Free the graph buffer and all owned data. NULL-safe. */
void pmm_gbuf_free(pmm_gbuf_t *gb);

/* Merge all nodes and edges from src into dst.
 * Nodes are merged by QN: on collision, src wins (updates dst node fields).
 * New nodes are inserted with their original IDs (from shared ID source).
 * Edges are remapped for any QN-colliding nodes, then inserted with dedup.
 * After merge, src can be safely freed (all data is copied).
 * Returns 0 on success, -1 on error. */
int pmm_gbuf_merge(pmm_gbuf_t *dst, pmm_gbuf_t *src);

/* ── Node operations ─────────────────────────────────────────────── */

/* Upsert a node by qualified name. Returns the temp ID.
 * All string fields are copied (buffer owns the copies).
 * Returns 0 on error. */
int64_t pmm_gbuf_upsert_node(pmm_gbuf_t *gb, const char *label, const char *name,
                             const char *qualified_name, const char *file_path, int start_line,
                             int end_line, const char *properties_json);

/* Find a node by qualified name. Returns NULL if not found. */
const pmm_gbuf_node_t *pmm_gbuf_find_by_qn(const pmm_gbuf_t *gb, const char *qn);

/* Find a node by temp ID. Returns NULL if not found. */
const pmm_gbuf_node_t *pmm_gbuf_find_by_id(const pmm_gbuf_t *gb, int64_t id);

/* Find nodes by label. Sets *out and *count. Caller does NOT free.
 * Returns 0 on success, -1 on error. */
int pmm_gbuf_find_by_label(const pmm_gbuf_t *gb, const char *label, const pmm_gbuf_node_t ***out,
                           int *count);

/* Find nodes by name (exact). Sets *out and *count. Caller does NOT free. */
int pmm_gbuf_find_by_name(const pmm_gbuf_t *gb, const char *name, const pmm_gbuf_node_t ***out,
                          int *count);

/* Count total nodes in buffer. */
int pmm_gbuf_node_count(const pmm_gbuf_t *gb);

/* Get the next ID that would be assigned. Used to initialize shared atomic counters. */
int64_t pmm_gbuf_next_id(const pmm_gbuf_t *gb);

/* Set the next ID counter. Used after merging worker gbufs to sync the main counter. */
void pmm_gbuf_set_next_id(pmm_gbuf_t *gb, int64_t next_id);

/* Delete all nodes with a label. Cascade-deletes referencing edges. */
int pmm_gbuf_delete_by_label(pmm_gbuf_t *gb, const char *label);

/* Delete all nodes for a given file path. Cascade-deletes referencing edges.
 * Used by incremental indexing to remove stale nodes before re-extraction. */
int pmm_gbuf_delete_by_file(pmm_gbuf_t *gb, const char *file_path);

/* Bulk-load all nodes and edges for a project from an existing SQLite DB
 * into this graph buffer. Returns 0 on success. */
int pmm_gbuf_load_from_db(pmm_gbuf_t *gb, const char *db_path, const char *project);

/* Iterate all live nodes (not deleted from QN index). */
typedef void (*pmm_gbuf_node_visitor_fn)(const pmm_gbuf_node_t *node, void *userdata);
void pmm_gbuf_foreach_node(const pmm_gbuf_t *gb, pmm_gbuf_node_visitor_fn fn, void *userdata);

/* Iterate all edges. */
typedef void (*pmm_gbuf_edge_visitor_fn)(const pmm_gbuf_edge_t *edge, void *userdata);
void pmm_gbuf_foreach_edge(const pmm_gbuf_t *gb, pmm_gbuf_edge_visitor_fn fn, void *userdata);

/* ── Edge operations ─────────────────────────────────────────────── */

/* Insert an edge. Deduplicates by (source_id, target_id, type).
 * On duplicate, merges properties (later wins). Returns edge temp ID.
 * Returns 0 on error. */
int64_t pmm_gbuf_insert_edge(pmm_gbuf_t *gb, int64_t source_id, int64_t target_id, const char *type,
                             const char *properties_json);

/* Find edges from source_id with given type.
 * Sets *out and *count. Caller does NOT free. */
int pmm_gbuf_find_edges_by_source_type(const pmm_gbuf_t *gb, int64_t source_id, const char *type,
                                       const pmm_gbuf_edge_t ***out, int *count);

/* Find edges to target_id with given type. */
int pmm_gbuf_find_edges_by_target_type(const pmm_gbuf_t *gb, int64_t target_id, const char *type,
                                       const pmm_gbuf_edge_t ***out, int *count);

/* Find all edges of a given type. */
int pmm_gbuf_find_edges_by_type(const pmm_gbuf_t *gb, const char *type,
                                const pmm_gbuf_edge_t ***out, int *count);

/* Count total edges. */
int pmm_gbuf_edge_count(const pmm_gbuf_t *gb);

/* Count edges of a given type. */
int pmm_gbuf_edge_count_by_type(const pmm_gbuf_t *gb, const char *type);

/* Delete all edges of a type. */
int pmm_gbuf_delete_edges_by_type(pmm_gbuf_t *gb, const char *type);

/* ── Vector storage (for semantic embeddings) ───────────────────── */

/* Store an int8-quantized vector for a node. The vector data is copied.
 * Called by pass_semantic_edges after computing RI vectors.
 * Vectors are carried through to pmm_write_db during the dump phase. */
int pmm_gbuf_store_vector(pmm_gbuf_t *gb, int64_t node_id, const uint8_t *vector, int vector_len);

/* Store an enriched token vector for query-time lookup.
 * Called by pass_semantic_edges after corpus finalization.
 * Token string and vector data are copied. */
int pmm_gbuf_store_token_vector(pmm_gbuf_t *gb, const char *token, const uint8_t *vector,
                                int vector_len, float idf);

/* ── Dump to SQLite ──────────────────────────────────────────────── */

/* Dump the entire buffer to a SQLite file using the direct page writer.
 * Assigns sequential final IDs and remaps edge references.
 * Returns 0 on success, -1 on error. */
int pmm_gbuf_dump_to_sqlite(pmm_gbuf_t *gb, const char *path);

/* Flush the buffer to an existing store via the store API.
 * Deletes existing project data first. Returns 0 on success. */
int pmm_gbuf_flush_to_store(pmm_gbuf_t *gb, pmm_store_t *store);

/* Merge the buffer into an existing store WITHOUT deleting existing data.
 * Upserts nodes, inserts edges. Used for incremental indexing.
 * Returns 0 on success. */
int pmm_gbuf_merge_into_store(pmm_gbuf_t *gb, pmm_store_t *store);

#endif /* PMM_GRAPH_BUFFER_H */
