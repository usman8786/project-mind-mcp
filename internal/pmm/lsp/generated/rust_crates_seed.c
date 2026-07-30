/* rust_crates_seed.c — Common-crate API seeds for the Rust LSP.
 *
 * Per RUST_LSP_FOLLOWUP.md A3: we don't run Cargo or download deps.
 * What we CAN do is hand-curate a small registered-method seed for the
 * most-used external crates (serde, tokio, anyhow, clap, regex, log,
 * futures, thiserror, reqwest, hyper, async-trait, parking_lot,
 * chrono, uuid, lazy_static, once_cell, rayon, …) so a project that
 * just `use`s these crates resolves the obvious calls instead of
 * leaving them all `unresolved`.
 *
 * Each entry is "best-effort" — we register the type with its trait
 * QNs in embedded_types and synthesize the high-frequency methods
 * with vaguely-correct return shapes. Anything we get wrong stays
 * `unresolved` rather than a wrong edge (per the doc's no-false-edge
 * policy: we only register what we're confident about).
 *
 * Designed to be appended to the existing stdlib register: takes the
 * same (reg, arena) pair. */

#include "../type_rep.h"
#include "../type_registry.h"
#include "../rust_lsp.h"
#include <string.h>

/* Shorthand. Mirrors the macros in rust_stdlib_data.c. */
#ifndef RUST_CRATES_SEED_INCLUDED
#define RUST_CRATES_SEED_INCLUDED 1

#define CADD_TYPE(qn, sn, iface) do {                              \
    CBMRegisteredType _rt;                                          \
    memset(&_rt, 0, sizeof(_rt));                                   \
    _rt.qualified_name = (qn);                                      \
    _rt.short_name = (sn);                                          \
    _rt.is_interface = (iface);                                     \
    pmm_registry_add_type(reg, _rt);                                \
} while (0)

#define CADD_FUNC(rcv, sn, qn, ret_type) do {                       \
    CBMRegisteredFunc _rf;                                           \
    memset(&_rf, 0, sizeof(_rf));                                    \
    _rf.qualified_name = (qn);                                       \
    _rf.short_name = (sn);                                            \
    _rf.receiver_type = (rcv);                                        \
    _rf.min_params = -1;                                              \
    const CBMType** _rta = (const CBMType**)pmm_arena_alloc(arena,    \
        2 * sizeof(const CBMType*));                                  \
    _rta[0] = (ret_type);                                             \
    _rta[1] = NULL;                                                   \
    _rf.signature = pmm_type_func(arena, NULL, NULL, _rta);           \
    pmm_registry_add_func(reg, _rf);                                  \
} while (0)

void pmm_rust_crates_register(CBMTypeRegistry* reg, CBMArena* arena) {
    const CBMType* t_unit  = pmm_type_builtin(arena, "()");
    const CBMType* t_bool  = pmm_type_builtin(arena, "bool");
    const CBMType* t_usize = pmm_type_builtin(arena, "usize");
    const CBMType* t_str_ref = pmm_type_reference(arena,
        pmm_type_builtin(arena, "str"));
    const CBMType* t_string = pmm_type_named(arena, "alloc.string.String");

    /* ── serde — Serialize / Deserialize / json ─────────────── */
    CADD_TYPE("serde.Serialize",      "Serialize",      true);
    CADD_TYPE("serde.Deserialize",    "Deserialize",    true);
    CADD_TYPE("serde.Serializer",     "Serializer",     true);
    CADD_TYPE("serde.Deserializer",   "Deserializer",   true);
    CADD_TYPE("serde.de.Error",       "Error",          true);
    CADD_TYPE("serde.ser.Error",      "Error",          true);
    CADD_TYPE("serde_json.Value",     "Value",          false);
    CADD_TYPE("serde_json.Map",       "Map",            false);
    CADD_TYPE("serde_json.Number",    "Number",         false);
    CADD_TYPE("serde_json.Error",     "Error",          false);

    CADD_FUNC("serde.Serialize",   "serialize",   "serde.Serialize.serialize",   pmm_type_unknown());
    CADD_FUNC("serde.Deserialize", "deserialize", "serde.Deserialize.deserialize", pmm_type_unknown());
    CADD_FUNC("serde_json.Value", "is_null",      "serde_json.Value.is_null",     t_bool);
    CADD_FUNC("serde_json.Value", "is_bool",      "serde_json.Value.is_bool",     t_bool);
    CADD_FUNC("serde_json.Value", "is_number",    "serde_json.Value.is_number",   t_bool);
    CADD_FUNC("serde_json.Value", "is_string",    "serde_json.Value.is_string",   t_bool);
    CADD_FUNC("serde_json.Value", "is_array",     "serde_json.Value.is_array",    t_bool);
    CADD_FUNC("serde_json.Value", "is_object",    "serde_json.Value.is_object",   t_bool);
    CADD_FUNC("serde_json.Value", "as_bool",      "serde_json.Value.as_bool",     pmm_type_unknown());
    CADD_FUNC("serde_json.Value", "as_i64",       "serde_json.Value.as_i64",      pmm_type_unknown());
    CADD_FUNC("serde_json.Value", "as_f64",       "serde_json.Value.as_f64",      pmm_type_unknown());
    CADD_FUNC("serde_json.Value", "as_str",       "serde_json.Value.as_str",      pmm_type_unknown());
    CADD_FUNC("serde_json.Value", "as_array",     "serde_json.Value.as_array",    pmm_type_unknown());
    CADD_FUNC("serde_json.Value", "as_object",    "serde_json.Value.as_object",   pmm_type_unknown());
    CADD_FUNC("serde_json.Value", "get",          "serde_json.Value.get",         pmm_type_unknown());
    CADD_FUNC("serde_json.Value", "pointer",      "serde_json.Value.pointer",     pmm_type_unknown());
    CADD_FUNC("serde_json.Value", "to_string",    "serde_json.Value.to_string",   t_string);
    /* Free functions in serde_json. */
    CADD_FUNC(NULL, "from_str",  "serde_json.from_str",  pmm_type_unknown());
    CADD_FUNC(NULL, "to_string", "serde_json.to_string", pmm_type_unknown());
    CADD_FUNC(NULL, "from_value","serde_json.from_value",pmm_type_unknown());
    CADD_FUNC(NULL, "to_value",  "serde_json.to_value",  pmm_type_unknown());
    CADD_FUNC(NULL, "json",      "serde_json.json",      pmm_type_named(arena, "serde_json.Value"));

    /* ── anyhow — Error / Result / ensure!/bail!/anyhow! ───── */
    CADD_TYPE("anyhow.Error",   "Error",   false);
    CADD_TYPE("anyhow.Result",  "Result",  false);
    CADD_TYPE("anyhow.Chain",   "Chain",   false);
    CADD_FUNC("anyhow.Error",   "new",      "anyhow.Error.new",      pmm_type_named(arena, "anyhow.Error"));
    CADD_FUNC("anyhow.Error",   "from",     "anyhow.Error.from",     pmm_type_named(arena, "anyhow.Error"));
    CADD_FUNC("anyhow.Error",   "msg",      "anyhow.Error.msg",      pmm_type_named(arena, "anyhow.Error"));
    CADD_FUNC("anyhow.Error",   "context",  "anyhow.Error.context",  pmm_type_named(arena, "anyhow.Error"));
    CADD_FUNC("anyhow.Error",   "downcast", "anyhow.Error.downcast", pmm_type_unknown());
    CADD_FUNC("anyhow.Error",   "downcast_ref","anyhow.Error.downcast_ref", pmm_type_unknown());
    CADD_FUNC("anyhow.Error",   "is",       "anyhow.Error.is",       t_bool);
    CADD_FUNC("anyhow.Error",   "chain",    "anyhow.Error.chain",    pmm_type_unknown());
    CADD_FUNC("anyhow.Error",   "root_cause","anyhow.Error.root_cause", pmm_type_unknown());
    CADD_FUNC("anyhow.Error",   "source",   "anyhow.Error.source",   pmm_type_unknown());
    /* Free functions. */
    CADD_FUNC(NULL, "anyhow",  "anyhow.anyhow",  pmm_type_named(arena, "anyhow.Error"));
    CADD_FUNC(NULL, "bail",    "anyhow.bail",    pmm_type_unknown());
    CADD_FUNC(NULL, "ensure",  "anyhow.ensure",  pmm_type_unknown());

    /* ── thiserror — derive-only crate, but the Error trait. ─── */
    /* (Error trait is already covered under core.error.Error.) */

    /* ── tokio — runtime + I/O + sync primitives. ───────────── */
    CADD_TYPE("tokio.runtime.Runtime",   "Runtime",   false);
    CADD_TYPE("tokio.runtime.Handle",    "Handle",    false);
    CADD_TYPE("tokio.runtime.Builder",   "Builder",   false);
    CADD_TYPE("tokio.task.JoinHandle",   "JoinHandle",false);
    CADD_TYPE("tokio.sync.Mutex",        "Mutex",     false);
    CADD_TYPE("tokio.sync.RwLock",       "RwLock",    false);
    CADD_TYPE("tokio.sync.Semaphore",    "Semaphore", false);
    CADD_TYPE("tokio.sync.Notify",       "Notify",    false);
    CADD_TYPE("tokio.sync.oneshot.Sender","Sender",   false);
    CADD_TYPE("tokio.sync.oneshot.Receiver","Receiver", false);
    CADD_TYPE("tokio.sync.mpsc.Sender",   "Sender",   false);
    CADD_TYPE("tokio.sync.mpsc.Receiver", "Receiver", false);
    CADD_TYPE("tokio.sync.mpsc.UnboundedSender","UnboundedSender", false);
    CADD_TYPE("tokio.sync.mpsc.UnboundedReceiver","UnboundedReceiver", false);
    CADD_TYPE("tokio.net.TcpListener",   "TcpListener", false);
    CADD_TYPE("tokio.net.TcpStream",     "TcpStream",   false);
    CADD_TYPE("tokio.io.BufReader",      "BufReader",   false);
    CADD_TYPE("tokio.io.BufWriter",      "BufWriter",   false);
    CADD_TYPE("tokio.time.Sleep",        "Sleep",       false);
    CADD_TYPE("tokio.time.Interval",     "Interval",    false);
    CADD_TYPE("tokio.fs.File",           "File",        false);
    CADD_TYPE("tokio.process.Command",   "Command",     false);
    CADD_TYPE("tokio.process.Child",     "Child",       false);

    CADD_FUNC("tokio.runtime.Runtime", "new",       "tokio.runtime.Runtime.new",      pmm_type_unknown());
    CADD_FUNC("tokio.runtime.Runtime", "block_on", "tokio.runtime.Runtime.block_on",  pmm_type_unknown());
    CADD_FUNC("tokio.runtime.Runtime", "spawn",    "tokio.runtime.Runtime.spawn",     pmm_type_unknown());
    CADD_FUNC("tokio.runtime.Runtime", "handle",   "tokio.runtime.Runtime.handle",    pmm_type_unknown());
    CADD_FUNC("tokio.runtime.Runtime", "shutdown_timeout","tokio.runtime.Runtime.shutdown_timeout", t_unit);
    CADD_FUNC("tokio.runtime.Builder", "new_multi_thread","tokio.runtime.Builder.new_multi_thread", pmm_type_unknown());
    CADD_FUNC("tokio.runtime.Builder", "new_current_thread","tokio.runtime.Builder.new_current_thread", pmm_type_unknown());
    CADD_FUNC("tokio.runtime.Builder", "worker_threads","tokio.runtime.Builder.worker_threads", pmm_type_unknown());
    CADD_FUNC("tokio.runtime.Builder", "enable_all","tokio.runtime.Builder.enable_all", pmm_type_unknown());
    CADD_FUNC("tokio.runtime.Builder", "build",    "tokio.runtime.Builder.build",     pmm_type_unknown());
    CADD_FUNC("tokio.runtime.Handle",  "current",  "tokio.runtime.Handle.current",    pmm_type_unknown());
    CADD_FUNC("tokio.runtime.Handle",  "spawn",    "tokio.runtime.Handle.spawn",      pmm_type_unknown());
    CADD_FUNC("tokio.runtime.Handle",  "block_on", "tokio.runtime.Handle.block_on",   pmm_type_unknown());
    CADD_FUNC("tokio.task.JoinHandle", "await",    "tokio.task.JoinHandle.await",     pmm_type_unknown());
    CADD_FUNC("tokio.task.JoinHandle", "abort",    "tokio.task.JoinHandle.abort",     t_unit);
    CADD_FUNC("tokio.task.JoinHandle", "is_finished","tokio.task.JoinHandle.is_finished", t_bool);
    CADD_FUNC("tokio.sync.Mutex",      "new",      "tokio.sync.Mutex.new",            pmm_type_unknown());
    CADD_FUNC("tokio.sync.Mutex",      "lock",     "tokio.sync.Mutex.lock",           pmm_type_unknown());
    CADD_FUNC("tokio.sync.Mutex",      "try_lock", "tokio.sync.Mutex.try_lock",       pmm_type_unknown());
    CADD_FUNC("tokio.sync.RwLock",     "new",      "tokio.sync.RwLock.new",           pmm_type_unknown());
    CADD_FUNC("tokio.sync.RwLock",     "read",     "tokio.sync.RwLock.read",          pmm_type_unknown());
    CADD_FUNC("tokio.sync.RwLock",     "write",    "tokio.sync.RwLock.write",         pmm_type_unknown());
    CADD_FUNC("tokio.sync.Semaphore",  "new",      "tokio.sync.Semaphore.new",        pmm_type_unknown());
    CADD_FUNC("tokio.sync.Semaphore",  "acquire",  "tokio.sync.Semaphore.acquire",    pmm_type_unknown());
    CADD_FUNC("tokio.sync.Semaphore",  "add_permits","tokio.sync.Semaphore.add_permits", t_unit);
    CADD_FUNC("tokio.sync.Notify",     "new",      "tokio.sync.Notify.new",           pmm_type_unknown());
    CADD_FUNC("tokio.sync.Notify",     "notified", "tokio.sync.Notify.notified",      pmm_type_unknown());
    CADD_FUNC("tokio.sync.Notify",     "notify_one","tokio.sync.Notify.notify_one",   t_unit);
    CADD_FUNC("tokio.sync.Notify",     "notify_waiters","tokio.sync.Notify.notify_waiters", t_unit);
    CADD_FUNC("tokio.sync.mpsc.Sender",  "send",   "tokio.sync.mpsc.Sender.send",     pmm_type_unknown());
    CADD_FUNC("tokio.sync.mpsc.Sender",  "try_send","tokio.sync.mpsc.Sender.try_send",pmm_type_unknown());
    CADD_FUNC("tokio.sync.mpsc.Receiver","recv",   "tokio.sync.mpsc.Receiver.recv",   pmm_type_unknown());
    CADD_FUNC("tokio.sync.mpsc.Receiver","try_recv","tokio.sync.mpsc.Receiver.try_recv",pmm_type_unknown());
    CADD_FUNC("tokio.sync.oneshot.Sender","send",  "tokio.sync.oneshot.Sender.send",  pmm_type_unknown());
    CADD_FUNC("tokio.sync.oneshot.Receiver","await","tokio.sync.oneshot.Receiver.await", pmm_type_unknown());
    CADD_FUNC("tokio.net.TcpListener", "bind",     "tokio.net.TcpListener.bind",      pmm_type_unknown());
    CADD_FUNC("tokio.net.TcpListener", "accept",   "tokio.net.TcpListener.accept",    pmm_type_unknown());
    CADD_FUNC("tokio.net.TcpListener", "local_addr","tokio.net.TcpListener.local_addr",pmm_type_unknown());
    CADD_FUNC("tokio.net.TcpStream",   "connect",  "tokio.net.TcpStream.connect",     pmm_type_unknown());
    CADD_FUNC("tokio.net.TcpStream",   "peer_addr","tokio.net.TcpStream.peer_addr",   pmm_type_unknown());
    CADD_FUNC("tokio.time.Sleep",      "await",    "tokio.time.Sleep.await",          t_unit);
    CADD_FUNC("tokio.time.Interval",   "tick",     "tokio.time.Interval.tick",        pmm_type_unknown());
    CADD_FUNC("tokio.fs.File",         "open",     "tokio.fs.File.open",              pmm_type_unknown());
    CADD_FUNC("tokio.fs.File",         "create",   "tokio.fs.File.create",            pmm_type_unknown());
    /* Free functions. */
    CADD_FUNC(NULL, "spawn",    "tokio.spawn",     pmm_type_named(arena, "tokio.task.JoinHandle"));
    CADD_FUNC(NULL, "spawn_blocking","tokio.task.spawn_blocking", pmm_type_named(arena, "tokio.task.JoinHandle"));
    CADD_FUNC(NULL, "yield_now","tokio.task.yield_now", t_unit);
    CADD_FUNC(NULL, "sleep",    "tokio.time.sleep", pmm_type_named(arena, "tokio.time.Sleep"));
    CADD_FUNC(NULL, "sleep_until","tokio.time.sleep_until", pmm_type_named(arena, "tokio.time.Sleep"));
    CADD_FUNC(NULL, "interval", "tokio.time.interval", pmm_type_named(arena, "tokio.time.Interval"));
    CADD_FUNC(NULL, "timeout",  "tokio.time.timeout", pmm_type_unknown());
    CADD_FUNC(NULL, "read_to_string","tokio.fs.read_to_string", pmm_type_unknown());
    CADD_FUNC(NULL, "write",    "tokio.fs.write",  pmm_type_unknown());
    CADD_FUNC(NULL, "read",     "tokio.fs.read",   pmm_type_unknown());
    CADD_FUNC(NULL, "channel",  "tokio.sync.mpsc.channel", pmm_type_unknown());
    CADD_FUNC(NULL, "unbounded_channel","tokio.sync.mpsc.unbounded_channel", pmm_type_unknown());
    CADD_FUNC(NULL, "join",     "tokio.join",      pmm_type_unknown());
    CADD_FUNC(NULL, "try_join", "tokio.try_join",  pmm_type_unknown());
    CADD_FUNC(NULL, "select",   "tokio.select",    pmm_type_unknown());

    /* ── clap — argument parsing. ─────────────────────────── */
    CADD_TYPE("clap.Parser",     "Parser",     true);
    CADD_TYPE("clap.Args",       "Args",       true);
    CADD_TYPE("clap.Subcommand", "Subcommand", true);
    CADD_TYPE("clap.ValueEnum",  "ValueEnum",  true);
    CADD_TYPE("clap.Command",    "Command",    false);
    CADD_TYPE("clap.Arg",        "Arg",        false);
    CADD_TYPE("clap.ArgMatches", "ArgMatches", false);

    CADD_FUNC("clap.Parser", "parse",         "clap.Parser.parse",         pmm_type_unknown());
    CADD_FUNC("clap.Parser", "try_parse",     "clap.Parser.try_parse",     pmm_type_unknown());
    CADD_FUNC("clap.Parser", "parse_from",    "clap.Parser.parse_from",    pmm_type_unknown());
    CADD_FUNC("clap.Parser", "try_parse_from","clap.Parser.try_parse_from",pmm_type_unknown());
    CADD_FUNC("clap.Parser", "command",       "clap.Parser.command",       pmm_type_named(arena, "clap.Command"));
    CADD_FUNC("clap.Command","new",           "clap.Command.new",          pmm_type_named(arena, "clap.Command"));
    CADD_FUNC("clap.Command","arg",           "clap.Command.arg",          pmm_type_named(arena, "clap.Command"));
    CADD_FUNC("clap.Command","subcommand",    "clap.Command.subcommand",   pmm_type_named(arena, "clap.Command"));
    CADD_FUNC("clap.Command","about",         "clap.Command.about",        pmm_type_named(arena, "clap.Command"));
    CADD_FUNC("clap.Command","version",       "clap.Command.version",      pmm_type_named(arena, "clap.Command"));
    CADD_FUNC("clap.Command","author",        "clap.Command.author",       pmm_type_named(arena, "clap.Command"));
    CADD_FUNC("clap.Command","get_matches",   "clap.Command.get_matches",  pmm_type_named(arena, "clap.ArgMatches"));
    CADD_FUNC("clap.Command","get_matches_from","clap.Command.get_matches_from", pmm_type_named(arena, "clap.ArgMatches"));
    CADD_FUNC("clap.Arg",    "new",           "clap.Arg.new",              pmm_type_named(arena, "clap.Arg"));
    CADD_FUNC("clap.Arg",    "short",         "clap.Arg.short",            pmm_type_named(arena, "clap.Arg"));
    CADD_FUNC("clap.Arg",    "long",          "clap.Arg.long",             pmm_type_named(arena, "clap.Arg"));
    CADD_FUNC("clap.Arg",    "value_name",    "clap.Arg.value_name",       pmm_type_named(arena, "clap.Arg"));
    CADD_FUNC("clap.Arg",    "required",      "clap.Arg.required",         pmm_type_named(arena, "clap.Arg"));
    CADD_FUNC("clap.Arg",    "default_value", "clap.Arg.default_value",    pmm_type_named(arena, "clap.Arg"));
    CADD_FUNC("clap.Arg",    "help",          "clap.Arg.help",             pmm_type_named(arena, "clap.Arg"));
    CADD_FUNC("clap.ArgMatches","get_one",    "clap.ArgMatches.get_one",   pmm_type_unknown());
    CADD_FUNC("clap.ArgMatches","get_many",   "clap.ArgMatches.get_many",  pmm_type_unknown());
    CADD_FUNC("clap.ArgMatches","get_flag",   "clap.ArgMatches.get_flag",  t_bool);
    CADD_FUNC("clap.ArgMatches","contains_id","clap.ArgMatches.contains_id",t_bool);
    CADD_FUNC("clap.ArgMatches","subcommand", "clap.ArgMatches.subcommand",pmm_type_unknown());

    /* ── regex — pattern matching. ────────────────────────── */
    CADD_TYPE("regex.Regex",     "Regex",     false);
    CADD_TYPE("regex.Captures",  "Captures",  false);
    CADD_TYPE("regex.Match",     "Match",     false);
    CADD_TYPE("regex.RegexBuilder","RegexBuilder", false);

    CADD_FUNC("regex.Regex", "new",         "regex.Regex.new",         pmm_type_unknown());
    CADD_FUNC("regex.Regex", "is_match",    "regex.Regex.is_match",    t_bool);
    CADD_FUNC("regex.Regex", "find",        "regex.Regex.find",        pmm_type_unknown());
    CADD_FUNC("regex.Regex", "find_iter",   "regex.Regex.find_iter",   pmm_type_unknown());
    CADD_FUNC("regex.Regex", "captures",    "regex.Regex.captures",    pmm_type_unknown());
    CADD_FUNC("regex.Regex", "captures_iter","regex.Regex.captures_iter", pmm_type_unknown());
    CADD_FUNC("regex.Regex", "replace",     "regex.Regex.replace",     t_string);
    CADD_FUNC("regex.Regex", "replace_all", "regex.Regex.replace_all", t_string);
    CADD_FUNC("regex.Regex", "split",       "regex.Regex.split",       pmm_type_unknown());
    CADD_FUNC("regex.Regex", "as_str",      "regex.Regex.as_str",      t_str_ref);
    CADD_FUNC("regex.Captures","get",       "regex.Captures.get",      pmm_type_unknown());
    CADD_FUNC("regex.Captures","name",      "regex.Captures.name",     pmm_type_unknown());
    CADD_FUNC("regex.Captures","len",       "regex.Captures.len",      t_usize);
    CADD_FUNC("regex.Match",   "as_str",    "regex.Match.as_str",      t_str_ref);
    CADD_FUNC("regex.Match",   "start",     "regex.Match.start",       t_usize);
    CADD_FUNC("regex.Match",   "end",       "regex.Match.end",         t_usize);
    CADD_FUNC("regex.Match",   "range",     "regex.Match.range",       pmm_type_unknown());

    /* ── log — logging macros + Logger trait. ─────────────── */
    CADD_TYPE("log.Logger",      "Logger",      true);
    CADD_TYPE("log.Level",       "Level",       false);
    CADD_TYPE("log.LevelFilter", "LevelFilter", false);
    CADD_TYPE("log.Metadata",    "Metadata",    false);
    CADD_TYPE("log.Record",      "Record",      false);
    /* The macros log!/info!/warn!/error!/debug!/trace! are matched by
     * name in rust_lsp.c's macro handler — register them as void
     * synthetic functions so the resolver can attribute them. */
    CADD_FUNC(NULL, "info",  "log.info",  t_unit);
    CADD_FUNC(NULL, "warn",  "log.warn",  t_unit);
    CADD_FUNC(NULL, "error", "log.error", t_unit);
    CADD_FUNC(NULL, "debug", "log.debug", t_unit);
    CADD_FUNC(NULL, "trace", "log.trace", t_unit);
    CADD_FUNC(NULL, "log",   "log.log",   t_unit);
    CADD_FUNC(NULL, "set_logger","log.set_logger", pmm_type_unknown());
    CADD_FUNC(NULL, "set_max_level","log.set_max_level", t_unit);

    /* ── futures — Future combinators + stream / sink. ────── */
    CADD_TYPE("futures.Future",   "Future",   true);
    CADD_TYPE("futures.Stream",   "Stream",   true);
    CADD_TYPE("futures.Sink",     "Sink",     true);
    CADD_TYPE("futures.StreamExt","StreamExt",true);
    CADD_TYPE("futures.FutureExt","FutureExt",true);
    CADD_TYPE("futures.SinkExt",  "SinkExt",  true);
    CADD_TYPE("futures.executor.LocalPool","LocalPool", false);

    CADD_FUNC("futures.StreamExt", "next",        "futures.StreamExt.next",        pmm_type_unknown());
    CADD_FUNC("futures.StreamExt", "map",         "futures.StreamExt.map",         pmm_type_unknown());
    CADD_FUNC("futures.StreamExt", "filter",      "futures.StreamExt.filter",      pmm_type_unknown());
    CADD_FUNC("futures.StreamExt", "filter_map",  "futures.StreamExt.filter_map",  pmm_type_unknown());
    CADD_FUNC("futures.StreamExt", "for_each",    "futures.StreamExt.for_each",    pmm_type_unknown());
    CADD_FUNC("futures.StreamExt", "collect",     "futures.StreamExt.collect",     pmm_type_unknown());
    CADD_FUNC("futures.StreamExt", "fold",        "futures.StreamExt.fold",        pmm_type_unknown());
    CADD_FUNC("futures.StreamExt", "take",        "futures.StreamExt.take",        pmm_type_unknown());
    CADD_FUNC("futures.StreamExt", "skip",        "futures.StreamExt.skip",        pmm_type_unknown());
    CADD_FUNC("futures.StreamExt", "chunks",      "futures.StreamExt.chunks",      pmm_type_unknown());
    CADD_FUNC("futures.StreamExt", "buffered",    "futures.StreamExt.buffered",    pmm_type_unknown());
    CADD_FUNC("futures.SinkExt",   "send",        "futures.SinkExt.send",          pmm_type_unknown());
    CADD_FUNC("futures.SinkExt",   "send_all",    "futures.SinkExt.send_all",      pmm_type_unknown());
    CADD_FUNC("futures.SinkExt",   "flush",       "futures.SinkExt.flush",         pmm_type_unknown());
    CADD_FUNC("futures.SinkExt",   "close",       "futures.SinkExt.close",         pmm_type_unknown());
    CADD_FUNC("futures.FutureExt", "boxed",       "futures.FutureExt.boxed",       pmm_type_unknown());
    CADD_FUNC("futures.FutureExt", "fuse",        "futures.FutureExt.fuse",        pmm_type_unknown());
    CADD_FUNC("futures.FutureExt", "shared",      "futures.FutureExt.shared",      pmm_type_unknown());

    CADD_FUNC(NULL, "join_all",     "futures.future.join_all",       pmm_type_unknown());
    CADD_FUNC(NULL, "try_join_all", "futures.future.try_join_all",   pmm_type_unknown());
    CADD_FUNC(NULL, "select",       "futures.future.select",         pmm_type_unknown());
    CADD_FUNC(NULL, "ready",        "futures.future.ready",          pmm_type_unknown());

    /* ── parking_lot — drop-in std::sync alternatives. ────── */
    CADD_TYPE("parking_lot.Mutex",        "Mutex",        false);
    CADD_TYPE("parking_lot.RwLock",       "RwLock",       false);
    CADD_TYPE("parking_lot.MutexGuard",   "MutexGuard",   false);
    CADD_TYPE("parking_lot.RwLockReadGuard","RwLockReadGuard", false);
    CADD_TYPE("parking_lot.RwLockWriteGuard","RwLockWriteGuard", false);
    CADD_TYPE("parking_lot.Condvar",      "Condvar",      false);
    CADD_TYPE("parking_lot.Once",         "Once",         false);

    CADD_FUNC("parking_lot.Mutex",  "new",      "parking_lot.Mutex.new",      pmm_type_unknown());
    CADD_FUNC("parking_lot.Mutex",  "lock",     "parking_lot.Mutex.lock",     pmm_type_unknown());
    CADD_FUNC("parking_lot.Mutex",  "try_lock", "parking_lot.Mutex.try_lock", pmm_type_unknown());
    CADD_FUNC("parking_lot.RwLock", "new",      "parking_lot.RwLock.new",     pmm_type_unknown());
    CADD_FUNC("parking_lot.RwLock", "read",     "parking_lot.RwLock.read",    pmm_type_unknown());
    CADD_FUNC("parking_lot.RwLock", "write",    "parking_lot.RwLock.write",   pmm_type_unknown());

    /* ── lazy_static / once_cell — singleton init. ────────── */
    CADD_TYPE("once_cell.sync.Lazy",  "Lazy",  false);
    CADD_TYPE("once_cell.sync.OnceCell","OnceCell", false);
    CADD_TYPE("once_cell.unsync.Lazy","Lazy",  false);
    CADD_FUNC("once_cell.sync.Lazy","new","once_cell.sync.Lazy.new",pmm_type_unknown());
    CADD_FUNC("once_cell.sync.OnceCell","new","once_cell.sync.OnceCell.new",pmm_type_unknown());
    CADD_FUNC("once_cell.sync.OnceCell","get","once_cell.sync.OnceCell.get",pmm_type_unknown());
    CADD_FUNC("once_cell.sync.OnceCell","set","once_cell.sync.OnceCell.set",pmm_type_unknown());
    CADD_FUNC("once_cell.sync.OnceCell","get_or_init","once_cell.sync.OnceCell.get_or_init", pmm_type_unknown());

    /* ── chrono — date/time. ────────────────────────────── */
    CADD_TYPE("chrono.DateTime",     "DateTime",     false);
    CADD_TYPE("chrono.NaiveDate",    "NaiveDate",    false);
    CADD_TYPE("chrono.NaiveTime",    "NaiveTime",    false);
    CADD_TYPE("chrono.NaiveDateTime","NaiveDateTime",false);
    CADD_TYPE("chrono.Duration",     "Duration",     false);
    CADD_TYPE("chrono.Utc",          "Utc",          false);
    CADD_TYPE("chrono.Local",        "Local",        false);

    CADD_FUNC("chrono.DateTime",     "now",       "chrono.DateTime.now",       pmm_type_unknown());
    CADD_FUNC("chrono.DateTime",     "format",    "chrono.DateTime.format",    pmm_type_unknown());
    CADD_FUNC("chrono.DateTime",     "timestamp", "chrono.DateTime.timestamp", pmm_type_unknown());
    CADD_FUNC("chrono.DateTime",     "to_rfc3339","chrono.DateTime.to_rfc3339",t_string);
    CADD_FUNC("chrono.Utc",          "now",       "chrono.Utc.now",            pmm_type_unknown());
    CADD_FUNC("chrono.Local",        "now",       "chrono.Local.now",          pmm_type_unknown());
    CADD_FUNC("chrono.Duration",     "seconds",   "chrono.Duration.seconds",   pmm_type_unknown());
    CADD_FUNC("chrono.Duration",     "minutes",   "chrono.Duration.minutes",   pmm_type_unknown());
    CADD_FUNC("chrono.Duration",     "hours",     "chrono.Duration.hours",     pmm_type_unknown());
    CADD_FUNC("chrono.Duration",     "days",      "chrono.Duration.days",      pmm_type_unknown());

    /* ── uuid — IDs. ────────────────────────────────────── */
    CADD_TYPE("uuid.Uuid", "Uuid", false);
    CADD_FUNC("uuid.Uuid", "new_v4",     "uuid.Uuid.new_v4",     pmm_type_unknown());
    CADD_FUNC("uuid.Uuid", "nil",        "uuid.Uuid.nil",        pmm_type_unknown());
    CADD_FUNC("uuid.Uuid", "parse_str",  "uuid.Uuid.parse_str",  pmm_type_unknown());
    CADD_FUNC("uuid.Uuid", "to_string",  "uuid.Uuid.to_string",  t_string);
    CADD_FUNC("uuid.Uuid", "to_hyphenated","uuid.Uuid.to_hyphenated", pmm_type_unknown());
    CADD_FUNC("uuid.Uuid", "as_bytes",   "uuid.Uuid.as_bytes",   pmm_type_unknown());

    /* ── reqwest — HTTP client. ──────────────────────────── */
    CADD_TYPE("reqwest.Client",       "Client",       false);
    CADD_TYPE("reqwest.RequestBuilder","RequestBuilder", false);
    CADD_TYPE("reqwest.Response",     "Response",     false);
    CADD_TYPE("reqwest.Error",        "Error",        false);
    CADD_TYPE("reqwest.Url",          "Url",          false);
    CADD_TYPE("reqwest.header.HeaderMap","HeaderMap", false);

    CADD_FUNC("reqwest.Client",  "new",      "reqwest.Client.new",      pmm_type_named(arena, "reqwest.Client"));
    CADD_FUNC("reqwest.Client",  "builder",  "reqwest.Client.builder",  pmm_type_unknown());
    CADD_FUNC("reqwest.Client",  "get",      "reqwest.Client.get",      pmm_type_named(arena, "reqwest.RequestBuilder"));
    CADD_FUNC("reqwest.Client",  "post",     "reqwest.Client.post",     pmm_type_named(arena, "reqwest.RequestBuilder"));
    CADD_FUNC("reqwest.Client",  "put",      "reqwest.Client.put",      pmm_type_named(arena, "reqwest.RequestBuilder"));
    CADD_FUNC("reqwest.Client",  "delete",   "reqwest.Client.delete",   pmm_type_named(arena, "reqwest.RequestBuilder"));
    CADD_FUNC("reqwest.Client",  "request",  "reqwest.Client.request",  pmm_type_named(arena, "reqwest.RequestBuilder"));
    CADD_FUNC("reqwest.RequestBuilder","header","reqwest.RequestBuilder.header", pmm_type_named(arena, "reqwest.RequestBuilder"));
    CADD_FUNC("reqwest.RequestBuilder","headers","reqwest.RequestBuilder.headers", pmm_type_named(arena, "reqwest.RequestBuilder"));
    CADD_FUNC("reqwest.RequestBuilder","body", "reqwest.RequestBuilder.body", pmm_type_named(arena, "reqwest.RequestBuilder"));
    CADD_FUNC("reqwest.RequestBuilder","json", "reqwest.RequestBuilder.json", pmm_type_named(arena, "reqwest.RequestBuilder"));
    CADD_FUNC("reqwest.RequestBuilder","form", "reqwest.RequestBuilder.form", pmm_type_named(arena, "reqwest.RequestBuilder"));
    CADD_FUNC("reqwest.RequestBuilder","query","reqwest.RequestBuilder.query", pmm_type_named(arena, "reqwest.RequestBuilder"));
    CADD_FUNC("reqwest.RequestBuilder","send", "reqwest.RequestBuilder.send", pmm_type_named(arena, "reqwest.Response"));
    CADD_FUNC("reqwest.Response","status",     "reqwest.Response.status",     pmm_type_unknown());
    CADD_FUNC("reqwest.Response","headers",    "reqwest.Response.headers",    pmm_type_unknown());
    CADD_FUNC("reqwest.Response","text",       "reqwest.Response.text",       pmm_type_unknown());
    CADD_FUNC("reqwest.Response","json",       "reqwest.Response.json",       pmm_type_unknown());
    CADD_FUNC("reqwest.Response","bytes",      "reqwest.Response.bytes",      pmm_type_unknown());
    CADD_FUNC("reqwest.Url",     "parse",      "reqwest.Url.parse",           pmm_type_unknown());

    /* ── rayon — parallel iteration. ────────────────────── */
    CADD_TYPE("rayon.iter.ParallelIterator","ParallelIterator", true);
    CADD_TYPE("rayon.iter.IntoParallelIterator","IntoParallelIterator", true);

    CADD_FUNC("rayon.iter.ParallelIterator","map",      "rayon.iter.ParallelIterator.map",      pmm_type_unknown());
    CADD_FUNC("rayon.iter.ParallelIterator","filter",   "rayon.iter.ParallelIterator.filter",   pmm_type_unknown());
    CADD_FUNC("rayon.iter.ParallelIterator","for_each", "rayon.iter.ParallelIterator.for_each", t_unit);
    CADD_FUNC("rayon.iter.ParallelIterator","collect",  "rayon.iter.ParallelIterator.collect",  pmm_type_unknown());
    CADD_FUNC("rayon.iter.ParallelIterator","reduce",   "rayon.iter.ParallelIterator.reduce",   pmm_type_unknown());
    CADD_FUNC("rayon.iter.ParallelIterator","sum",      "rayon.iter.ParallelIterator.sum",      pmm_type_unknown());
    CADD_FUNC("rayon.iter.ParallelIterator","count",    "rayon.iter.ParallelIterator.count",    t_usize);
    CADD_FUNC("rayon.iter.ParallelIterator","any",      "rayon.iter.ParallelIterator.any",      t_bool);
    CADD_FUNC("rayon.iter.ParallelIterator","all",      "rayon.iter.ParallelIterator.all",      t_bool);
    CADD_FUNC("rayon.iter.IntoParallelIterator","into_par_iter","rayon.iter.IntoParallelIterator.into_par_iter", pmm_type_unknown());

    CADD_FUNC(NULL, "scope",       "rayon.scope",        pmm_type_unknown());
    CADD_FUNC(NULL, "par_iter",    "rayon.par_iter",     pmm_type_unknown());
    CADD_FUNC(NULL, "spawn",       "rayon.spawn",        t_unit);
    CADD_FUNC(NULL, "join",        "rayon.join",         pmm_type_unknown());

    /* ── async-trait / async_trait — typically derive-only. ──
     * Calls to trait methods are resolved through the normal trait
     * dispatch since async_trait emits real Rust impl blocks. No
     * extra registration needed. */
    (void)t_str_ref;
}

#endif  /* RUST_CRATES_SEED_INCLUDED */
