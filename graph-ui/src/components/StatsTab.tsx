import { useMemo, useState, useCallback, useEffect, useRef } from "react";
import { ScrollArea } from "@/components/ui/scroll-area";
import { useProjects } from "../hooks/useProjects";
import { colorForLabel } from "../lib/colors";
import { useUiMessages } from "../lib/i18n";
import type { Project, SchemaInfo } from "../lib/types";

interface StatsTabProps {
  onSelectProject: (project: string) => void;
}

interface ProjectInfo {
  project: Project;
  schema: SchemaInfo | null;
}

function sumLabelCounts(schema: SchemaInfo | null): { nodes: number; edges: number } {
  if (!schema) return { nodes: 0, edges: 0 };
  const nodes =
    typeof schema.total_nodes === "number" && schema.total_nodes > 0
      ? schema.total_nodes
      : schema.node_labels?.reduce((s, l) => s + l.count, 0) ?? 0;
  const edges =
    typeof schema.total_edges === "number" && schema.total_edges > 0
      ? schema.total_edges
      : schema.edge_types?.reduce((s, t) => s + t.count, 0) ?? 0;
  return { nodes, edges };
}

/** Prefer list_projects counts; fall back to schema totals/label sums. */
function projectCounts(info: ProjectInfo): { nodes: number; edges: number } {
  const fromList = {
    nodes: typeof info.project.nodes === "number" ? info.project.nodes : null,
    edges: typeof info.project.edges === "number" ? info.project.edges : null,
  };
  if (fromList.nodes !== null && fromList.edges !== null) {
    return { nodes: fromList.nodes, edges: fromList.edges };
  }
  const fromSchema = sumLabelCounts(info.schema);
  return {
    nodes: fromList.nodes ?? fromSchema.nodes,
    edges: fromList.edges ?? fromSchema.edges,
  };
}

function displayName(project: Project): string {
  const path = (project.root_path || project.name).replace(/\\/g, "/");
  const parts = path.split("/").filter(Boolean);
  return parts[parts.length - 1] || project.name;
}

/* ── Documents button + modal ───────────────────────────── */

function DocsButton({ project }: { project: string }) {
  const t = useUiMessages();
  const [open, setOpen] = useState(false);
  const [docs, setDocs] = useState<{ path: string; kind: string }[]>([]);
  const [path, setPath] = useState("");
  const [busy, setBusy] = useState(false);

  const refresh = useCallback(async () => {
    try {
      const res = await fetch(`/api/docs?project=${encodeURIComponent(project)}`);
      const data = await res.json();
      setDocs(Array.isArray(data.docs) ? data.docs : []);
    } catch {
      setDocs([]);
    }
  }, [project]);

  useEffect(() => {
    if (open) refresh();
  }, [open, refresh]);

  const attach = async () => {
    if (!path.trim()) return;
    setBusy(true);
    try {
      await fetch("/api/docs", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ project, path: path.trim(), op: "attach" }),
      });
      setPath("");
      await refresh();
    } catch { /* ignore */ }
    finally { setBusy(false); }
  };

  const detach = async (p: string) => {
    setBusy(true);
    try {
      await fetch("/api/docs", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ project, path: p, op: "detach" }),
      });
      await refresh();
    } catch { /* ignore */ }
    finally { setBusy(false); }
  };

  return (
    <>
      <button
        onClick={() => setOpen(true)}
        className="px-2.5 py-1 rounded-lg text-[10px] font-medium transition-all bg-white/[0.03] text-muted-foreground/60 hover:text-muted-foreground hover:bg-white/[0.06]"
      >
        Docs
      </button>
      {open && (
        <div className="fixed inset-0 z-50 flex items-center justify-center" onClick={() => setOpen(false)}>
          <div className="absolute inset-0 bg-black/65 backdrop-blur-sm" />
          <div
            className="anim-modal-in relative surface-panel rounded-2xl p-6 w-full max-w-lg shadow-2xl max-h-[80vh] flex flex-col"
            onClick={(e) => e.stopPropagation()}
          >
            <div className="flex items-center justify-between mb-4">
              <div>
                <h3 className="text-[15px] font-semibold text-foreground">{t.docs.title}</h3>
                <p className="text-[11px] text-muted-foreground font-mono mt-0.5">{project}</p>
              </div>
              <button onClick={() => setOpen(false)} className="text-muted-foreground/40 hover:text-foreground text-[16px] p-1">×</button>
            </div>
            <p className="text-[11px] text-muted-foreground mb-3">{t.docs.hint}</p>
            <div className="flex gap-2 mb-4">
              <input
                value={path}
                onChange={(e) => setPath(e.target.value)}
                placeholder={t.docs.pathPlaceholder}
                className="flex-1 bg-background/60 border border-border/50 rounded-xl px-3 py-2 text-[12px] text-foreground font-mono outline-none focus:border-primary/40"
              />
              <button
                onClick={attach}
                disabled={busy}
                className="px-3 py-2 rounded-lg bg-primary/20 hover:bg-primary/30 text-primary text-[12px] font-medium disabled:opacity-30"
              >
                {t.docs.add}
              </button>
            </div>
            <div className="flex-1 overflow-auto space-y-2 min-h-[120px]">
              {docs.length === 0 && (
                <p className="text-[12px] text-muted-foreground/70">{t.docs.empty}</p>
              )}
              {docs.map((d) => (
                <div key={d.path} className="flex items-center justify-between gap-2 rounded-lg border border-border/40 px-3 py-2">
                  <div className="min-w-0">
                    <p className="text-[11px] font-mono text-foreground truncate">{d.path}</p>
                    {d.kind && <p className="text-[10px] text-muted-foreground">{d.kind}</p>}
                  </div>
                  <button
                    onClick={() => detach(d.path)}
                    disabled={busy}
                    className="text-[11px] text-destructive/70 hover:text-destructive"
                  >
                    {t.common.delete}
                  </button>
                </div>
              ))}
            </div>
          </div>
        </div>
      )}
    </>
  );
}

/* ── Glowy health dot ───────────────────────────────────── */

function HealthDot({ name }: { name: string }) {
  const t = useUiMessages();
  const [status, setStatus] = useState<"loading" | "healthy" | "corrupt" | "missing">("loading");
  const [info, setInfo] = useState("");

  useEffect(() => {
    fetch(`/api/project-health?name=${encodeURIComponent(name)}`)
      .then((r) => r.json())
      .then((d) => {
        setStatus(d.status ?? "corrupt");
        if (d.nodes !== undefined) {
          const sizeMB = ((d.size_bytes ?? 0) / 1024 / 1024).toFixed(1);
          setInfo(`${d.nodes.toLocaleString()} nodes, ${d.edges.toLocaleString()} edges, ${sizeMB} MB`);
        } else if (d.reason) {
          setInfo(d.reason);
        }
      })
      .catch(() => setStatus("corrupt"));
  }, [name]);

  const dotColor =
    status === "healthy" ? "#2dd4bf" :
    status === "missing" ? "#e8b86d" :
    status === "corrupt" ? "#f07178" : "#555";

  const label =
    status === "healthy" ? t.projects.healthHealthy :
    status === "missing" ? t.projects.healthMissing :
    status === "corrupt" ? t.projects.healthCorrupt : t.projects.healthChecking;

  return (
    <div className="group relative inline-flex items-center">
      <span
        className="absolute w-3 h-3 rounded-full animate-pulse opacity-40 blur-[3px]"
        style={{ backgroundColor: dotColor }}
      />
      <span
        className="relative w-[8px] h-[8px] rounded-full"
        style={{ backgroundColor: dotColor, boxShadow: `0 0 8px ${dotColor}80` }}
      />
      <div className="absolute bottom-full left-1/2 -translate-x-1/2 mb-3 hidden group-hover:block z-20 pointer-events-none">
        <div className="surface-panel rounded-lg px-3 py-2 text-[11px] whitespace-nowrap shadow-xl">
          <p className="font-medium" style={{ color: dotColor }}>{label}</p>
          {info && <p className="text-muted-foreground text-[10px] mt-0.5">{info}</p>}
        </div>
      </div>
    </div>
  );
}

/* ── ADR button + modal ─────────────────────────────────── */

function AdrButton({ project }: { project: string }) {
  const t = useUiMessages();
  const [hasAdr, setHasAdr] = useState<boolean | null>(null);
  const [open, setOpen] = useState(false);
  const [content, setContent] = useState("");
  const [saving, setSaving] = useState(false);
  const [updatedAt, setUpdatedAt] = useState("");

  const fetchAdr = useCallback(async () => {
    try {
      const res = await fetch(`/api/adr?project=${encodeURIComponent(project)}`);
      const data = await res.json();
      setHasAdr(data.has_adr ?? false);
      if (data.content) setContent(data.content);
      if (data.updated_at) setUpdatedAt(data.updated_at);
    } catch { setHasAdr(false); }
  }, [project]);

  useEffect(() => { fetchAdr(); }, [fetchAdr]);

  const save = async (nextContent = content) => {
    setSaving(true);
    try {
      await fetch("/api/adr", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ project, content: nextContent }),
      });
      await fetchAdr();
      setOpen(false);
    } catch { /* ignore */ }
    finally { setSaving(false); }
  };

  if (hasAdr === null) return null;

  return (
    <>
      <button
        onClick={() => { setOpen(true); fetchAdr(); }}
        className={`px-2.5 py-1 rounded-lg text-[10px] font-medium transition-all ${
          hasAdr
            ? "bg-accent/15 text-accent hover:bg-accent/25"
            : "bg-white/[0.03] text-muted-foreground/60 hover:text-muted-foreground hover:bg-white/[0.06]"
        }`}
      >
        {hasAdr ? "ADR" : "+ ADR"}
      </button>

      {open && (
        <div className="fixed inset-0 z-50 flex items-center justify-center" onClick={() => setOpen(false)}>
          <div className="absolute inset-0 bg-black/65 backdrop-blur-sm" />
          <div
            className="anim-modal-in relative surface-panel rounded-2xl p-6 w-full max-w-2xl shadow-2xl max-h-[80vh] flex flex-col"
            onClick={(e) => e.stopPropagation()}
          >
            <div className="flex items-center justify-between mb-4">
              <div>
                <h3 className="text-[15px] font-semibold text-foreground">{t.adr.title}</h3>
                <p className="text-[11px] text-muted-foreground font-mono mt-0.5">{project}</p>
              </div>
              <button onClick={() => setOpen(false)} className="text-muted-foreground/40 hover:text-foreground text-[16px] p-1">×</button>
            </div>
            {updatedAt && (
              <p className="text-[10px] text-muted-foreground/70 mb-3">{t.adr.lastUpdated}: {updatedAt}</p>
            )}
            <textarea
              value={content}
              onChange={(e) => setContent(e.target.value)}
              placeholder={"# Architecture Decision Record\n\n## Context\n...\n\n## Decision\n...\n\n## Consequences\n..."}
              className="flex-1 min-h-[300px] bg-background/60 border border-border/50 rounded-xl px-4 py-3 text-[12px] text-foreground font-mono placeholder-muted-foreground/40 outline-none focus:border-primary/40 resize-none leading-relaxed"
            />
            <div className="flex justify-end gap-2 mt-4">
              {hasAdr && (
                <button
                  onClick={async () => {
                    setContent(""); await save("");
                  }}
                  className="px-3 py-2 rounded-lg text-[12px] text-destructive/70 hover:text-destructive hover:bg-destructive/10 font-medium transition-all"
                >
                  {t.common.delete}
                </button>
              )}
              <button onClick={() => setOpen(false)} className="px-4 py-2 rounded-lg text-[12px] text-muted-foreground hover:bg-white/[0.04] font-medium transition-all">{t.common.cancel}</button>
              <button onClick={() => save()} disabled={saving} className="px-4 py-2 rounded-lg bg-primary/20 hover:bg-primary/30 text-primary text-[12px] font-medium transition-all disabled:opacity-30">
                {saving ? t.common.saving : t.common.save}
              </button>
            </div>
          </div>
        </div>
      )}
    </>
  );
}

/* ── Create Index Modal ─────────────────────────────────── */

function joinPath(base: string, dir: string): string {
  if (!base || base === "/") return `/${dir}`;
  if (/^[A-Za-z]:[\\/]?$/.test(base)) return `${base[0]}:/${dir}`;
  const slash = base.includes("\\") && !base.includes("/") ? "\\" : "/";
  return `${base.replace(/[\\/]+$/, "")}${slash}${dir}`;
}

function CreateIndexModal({ onClose, onCreated }: { onClose: () => void; onCreated: () => void }) {
  const t = useUiMessages();
  const [currentPath, setCurrentPath] = useState("");
  const [dirs, setDirs] = useState<string[]>([]);
  const [roots, setRoots] = useState<string[]>(["/"]);
  const [parentPath, setParentPath] = useState("");
  const [projectName, setProjectName] = useState("");
  const [filter, setFilter] = useState("");
  const [activeIndex, setActiveIndex] = useState(0);
  const [loading, setLoading] = useState(false);
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const filterRef = useRef<HTMLInputElement>(null);
  const lastBrowsedRef = useRef<string>("");

  const browse = useCallback(async (path?: string, opts?: { silent?: boolean }) => {
    const silent = opts?.silent ?? false;
    if (!silent) setLoading(true);
    setError(null);
    try {
      const q = path ? `?path=${encodeURIComponent(path)}` : "";
      const res = await fetch(`/api/browse${q}`);
      const data = await res.json();
      if (data.error) throw new Error(data.error);
      lastBrowsedRef.current = data.path ?? "";
      setCurrentPath(data.path ?? "");
      setDirs((data.dirs ?? []).sort());
      setRoots(data.roots ?? ["/"]);
      setParentPath(data.parent ?? "/");
    } catch (e) {
      if (!silent) setError(e instanceof Error ? e.message : "Browse failed");
    }
    finally { if (!silent) setLoading(false); }
  }, []);

  useEffect(() => { browse(); }, [browse]);
  useEffect(() => { filterRef.current?.focus(); }, []);

  useEffect(() => {
    if (!currentPath || currentPath === lastBrowsedRef.current) return;
    if (!/^[A-Za-z]:/.test(currentPath.replace(/\\/g, "/"))) return;
    const id = setTimeout(() => { void browse(currentPath, { silent: true }); }, 350);
    return () => clearTimeout(id);
  }, [currentPath, browse]);

  const filteredDirs = useMemo(() => {
    const q = filter.trim().toLowerCase();
    if (!q) return dirs;
    return dirs.filter((d) => d.toLowerCase().includes(q));
  }, [dirs, filter]);

  useEffect(() => { setActiveIndex(0); }, [filter, currentPath]);

  const submit = async (path = currentPath) => {
    if (!path) return;
    setSubmitting(true); setError(null);
    try {
      const body: { root_path: string; project_name?: string } = { root_path: path };
      if (projectName.trim()) body.project_name = projectName.trim();
      const res = await fetch("/api/index", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
      const data = await res.json();
      if (!res.ok) throw new Error(data.error ?? "Failed");
      onCreated(); onClose();
    } catch (e) { setError(e instanceof Error ? e.message : "Failed"); }
    finally { setSubmitting(false); }
  };

  const onFilterKeyDown = (e: React.KeyboardEvent<HTMLInputElement>) => {
    if (e.key === "ArrowDown") {
      e.preventDefault();
      setActiveIndex((i) => Math.min(i + 1, Math.max(filteredDirs.length - 1, 0)));
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      setActiveIndex((i) => Math.max(i - 1, 0));
    } else if (e.key === "Enter" && filteredDirs.length > 0) {
      e.preventDefault();
      const dir = filteredDirs.length === 1 ? filteredDirs[0] : filteredDirs[activeIndex];
      if (filteredDirs.length === 1) void submit(joinPath(currentPath, dir));
      else void browse(joinPath(currentPath, dir));
    }
  };

  const displayPath = currentPath.replace(/\\/g, "/");
  const segments = displayPath.split("/").filter(Boolean);
  const isWinPath = /^[A-Za-z]:$/.test(segments[0] ?? "");
  const crumbPath = (i: number): string => {
    const parts = segments.slice(0, i + 1);
    if (isWinPath) return parts.length === 1 ? `${parts[0]}/` : parts.join("/");
    return "/" + parts.join("/");
  };

  const displayRoots = (() => {
    if (!isWinPath) return roots;
    const drives = Array.from(new Set(
      roots.filter((r) => /^[A-Za-z]:[\\/]?$/.test(r)).map((r) => `${r[0].toUpperCase()}:/`),
    ));
    const curRoot = `${displayPath[0].toUpperCase()}:/`;
    if (!drives.includes(curRoot)) drives.unshift(curRoot);
    return drives;
  })();

  const homeJump = useMemo(() => {
    const m = displayPath.match(/^(\/home\/[^/]+)/);
    return m?.[1] ?? "/home/usman";
  }, [displayPath]);

  const windowsJump = "/mnt/c/Users/usman";
  const showPosixJumps = !isWinPath;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center" onClick={onClose}>
      <div className="absolute inset-0 bg-black/65 backdrop-blur-sm" />
      <div
        className="anim-modal-in relative surface-panel rounded-2xl w-full max-w-2xl shadow-2xl flex flex-col overflow-hidden"
        style={{ height: "min(82vh, 680px)" }}
        onClick={(e) => e.stopPropagation()}
      >
        <div className="px-5 pt-5 pb-3 shrink-0 border-b border-border/30">
          <h3 className="text-[16px] font-semibold text-foreground mb-1">{t.index.selectRepositoryFolder}</h3>
          <p className="text-[12px] text-muted-foreground">{t.index.instructions}</p>
          {showPosixJumps && (
            <p className="text-[11px] text-accent/80 mt-2">{t.index.wslHint}</p>
          )}
        </div>

        <div className="px-5 pt-4 pb-3 grid grid-cols-[1fr_220px] gap-3 shrink-0">
          <label className="block">
            <span className="block text-[10px] uppercase tracking-widest text-muted-foreground mb-1.5">{t.index.repositoryPath}</span>
            <input
              aria-label={t.index.repositoryPath}
              value={currentPath}
              onChange={(e) => setCurrentPath(e.target.value)}
              onKeyDown={(e) => { if (e.key === "Enter" && /^[A-Za-z]:/.test(currentPath.replace(/\\/g, "/"))) { e.preventDefault(); void browse(currentPath); } }}
              className="w-full bg-background/50 border border-border/50 rounded-lg px-3 py-2 text-[12px] text-foreground font-mono outline-none focus:border-primary/50"
            />
          </label>
          <label className="block">
            <span className="block text-[10px] uppercase tracking-widest text-muted-foreground mb-1.5">{t.index.projectName}</span>
            <input
              aria-label={t.index.projectName}
              value={projectName}
              placeholder={t.index.projectNamePlaceholder}
              onChange={(e) => setProjectName(e.target.value)}
              className="w-full bg-background/50 border border-border/50 rounded-lg px-3 py-2 text-[12px] text-foreground outline-none focus:border-primary/50 placeholder:text-muted-foreground/40"
            />
            <span className="block text-[10px] text-muted-foreground/70 mt-1">{t.index.projectNameHelp}</span>
          </label>
        </div>

        <div className="px-5 pb-3 flex flex-wrap items-center gap-2 shrink-0">
          <input
            ref={filterRef}
            value={filter}
            placeholder={t.index.filterFolders}
            onChange={(e) => setFilter(e.target.value)}
            onKeyDown={onFilterKeyDown}
            className="flex-1 min-w-[160px] bg-background/50 border border-border/50 rounded-lg px-3 py-2 text-[12px] text-foreground outline-none focus:border-primary/50 placeholder:text-muted-foreground/40"
          />
          {showPosixJumps && (
            <>
              <button
                type="button"
                onClick={() => browse(homeJump)}
                className="px-2.5 py-2 rounded-lg bg-accent/10 hover:bg-accent/20 text-[11px] text-accent font-medium transition-all"
              >
                {t.index.jumpHome}
              </button>
              <button
                type="button"
                onClick={() => browse(windowsJump)}
                className="px-2.5 py-2 rounded-lg bg-primary/10 hover:bg-primary/20 text-[11px] text-primary font-medium transition-all"
              >
                {t.index.jumpWindows}
              </button>
            </>
          )}
          <div className="flex items-center gap-1">
            {displayRoots.map((root) => (
              <button
                key={root}
                aria-label={t.index.browseRoot(root)}
                onClick={() => browse(root)}
                className="px-2.5 py-2 rounded-lg bg-white/[0.04] hover:bg-white/[0.07] text-[11px] text-muted-foreground font-mono transition-all"
              >
                {root}
              </button>
            ))}
          </div>
        </div>

        <div className="px-5 py-2.5 border-y border-border/30 flex items-center gap-0.5 overflow-x-auto text-[11px] shrink-0 bg-background/30">
          {!isWinPath && (
            <button onClick={() => browse("/")} className="text-primary/70 hover:text-primary shrink-0 transition-colors font-mono">/</button>
          )}
          {segments.map((seg, i) => (
            <span key={i} className="flex items-center gap-0.5 shrink-0">
              {(i > 0 || !isWinPath) && <span className="text-muted-foreground/30 font-mono">/</span>}
              <button
                onClick={() => browse(crumbPath(i))}
                className={`transition-colors font-mono ${i === segments.length - 1 ? "text-foreground font-medium" : "text-primary/60 hover:text-primary"}`}
              >
                {seg}
              </button>
            </span>
          ))}
        </div>

        <ScrollArea className="flex-1 min-h-0">
          <div className="px-2 py-1">
            {currentPath !== "/" && (
              <button
                onClick={() => browse(parentPath)}
                className="flex items-center gap-2 w-full text-left px-3 py-2 rounded-lg hover:bg-white/[0.04] text-[12px] text-muted-foreground transition-colors"
              >
                <span className="text-muted-foreground/40">↑</span>
                <span>..</span>
              </button>
            )}
            {loading ? (
              <p className="text-muted-foreground/50 text-[12px] text-center py-8">{t.common.loading}</p>
            ) : filteredDirs.length === 0 ? (
              <p className="text-muted-foreground/40 text-[12px] text-center py-8">{t.index.noSubdirectories}</p>
            ) : (
              filteredDirs.map((d, i) => (
                <div
                  key={d}
                  className={`flex items-center gap-2 rounded-lg px-3 py-1.5 text-[12px] transition-colors group ${
                    i === activeIndex ? "bg-primary/10" : "hover:bg-white/[0.04]"
                  }`}
                >
                  <button
                    aria-label={t.index.browseRoot(d)}
                    onClick={() => browse(joinPath(currentPath, d))}
                    className="flex min-w-0 flex-1 items-center gap-2 text-left text-foreground/75"
                  >
                    <span className="text-muted-foreground/35 group-hover:text-primary/50 font-mono">/</span>
                    <span className="truncate">{d}</span>
                  </button>
                  <button
                    aria-label={t.index.indexDirectory(d)}
                    onClick={() => submit(joinPath(currentPath, d))}
                    disabled={submitting}
                    className="opacity-100 sm:opacity-0 sm:group-hover:opacity-100 px-2 py-1 rounded-md bg-primary/15 hover:bg-primary/25 text-primary text-[10px] font-medium transition-all disabled:opacity-30"
                  >
                    {t.index.indexThisFolder}
                  </button>
                </div>
              ))
            )}
          </div>
        </ScrollArea>

        <div className="px-5 py-4 border-t border-border/30 shrink-0 bg-background/20">
          {error && (
            <div className="rounded-lg bg-destructive/10 border border-destructive/20 px-3 py-2 mb-3">
              <p className="text-destructive text-[11px]">{error}</p>
            </div>
          )}
          <div className="flex items-center justify-between gap-3">
            <p className="text-[11px] text-muted-foreground/60 font-mono truncate min-w-0">{currentPath}</p>
            <div className="flex gap-2 shrink-0">
              <button onClick={onClose} className="px-3 py-2 rounded-lg text-[12px] text-muted-foreground hover:bg-white/[0.04] font-medium transition-all">{t.common.cancel}</button>
              <button
                onClick={() => submit()}
                disabled={submitting || !currentPath}
                className="px-4 py-2 rounded-lg bg-primary text-primary-foreground text-[12px] font-semibold transition-all disabled:opacity-30 hover:brightness-110"
              >
                {submitting ? t.index.starting : t.index.indexThisFolder}
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

/* ── Index Progress ─────────────────────────────────────── */

export function IndexProgress({ onDone }: { onDone: () => void }) {
  const t = useUiMessages();
  const [jobs, setJobs] = useState<{ slot: number; status: string; path: string; error?: string }[]>([]);
  const [hasActive, setHasActive] = useState(true);
  useEffect(() => {
    if (!hasActive) return;
    const poll = setInterval(async () => {
      try {
        const data = await (await fetch("/api/index-status")).json();
        setJobs(data);
        const stillIndexing = data.some((j: { status: string }) => j.status === "indexing");
        if (data.length > 0 && !stillIndexing) {
          setHasActive(false);
          const hasErrors = data.some((j: { status: string }) => j.status === "error");
          if (!hasErrors) {
            onDone();
          }
        }
      } catch (error) {
        console.error("[IndexProgress] Poll failed:", error);
      }
    }, 2000);
    return () => clearInterval(poll);
  }, [onDone, hasActive]);

  const active = jobs.filter((j) => j.status === "indexing");
  const errors = jobs.filter((j) => j.status === "error");

  if (active.length === 0 && errors.length === 0) return null;

  return (
    <div className="rounded-xl border border-primary/25 bg-primary/5 p-4 mb-6">
      {active.map((j) => (
        <div key={j.slot} className="flex items-center gap-3">
          <div className="w-4 h-4 border-2 border-primary/30 border-t-primary rounded-full animate-spin shrink-0" />
          <div>
            <p className="text-[12px] text-primary font-medium">{t.projects.indexingInProgress}</p>
            <p className="text-[11px] text-muted-foreground font-mono">{j.path}</p>
          </div>
        </div>
      ))}
      {errors.map((j) => (
        <div key={j.slot} className="flex items-start gap-3 mt-3 first:mt-0 p-3 rounded-lg border border-destructive/20 bg-destructive/5 text-destructive">
          <span className="text-[14px]">!</span>
          <div className="flex-1 min-w-0">
            <p className="text-[12px] font-semibold">{t.projects.indexingFailed}</p>
            <p className="text-[11px] font-mono truncate">{j.path}</p>
            {j.error && <p className="text-[10px] opacity-75 mt-1 font-mono">{j.error}</p>}
          </div>
        </div>
      ))}
      {errors.length > 0 && (
        <div className="flex justify-end mt-3">
          <button
            onClick={onDone}
            className="px-3 py-1 rounded bg-destructive/10 hover:bg-destructive/20 text-destructive text-[11px] font-medium transition-all"
          >
            {t.common.dismiss}
          </button>
        </div>
      )}
    </div>
  );
}

/* ── Main Stats Tab ─────────────────────────────────────── */

export function StatsTab({ onSelectProject }: StatsTabProps) {
  const t = useUiMessages();
  const { projects, loading, error, refresh } = useProjects();
  const [showModal, setShowModal] = useState(false);
  const [indexing, setIndexing] = useState(false);

  const aggregate = useMemo(() => {
    let totalNodes = 0;
    let totalEdges = 0;
    for (const p of projects) {
      const c = projectCounts(p);
      totalNodes += c.nodes;
      totalEdges += c.edges;
    }
    return { projects: projects.length, nodes: totalNodes, edges: totalEdges };
  }, [projects]);

  const deleteProject = useCallback(async (name: string) => {
    if (!confirm(t.projects.deleteConfirm(name))) return;
    try { await fetch(`/api/project?name=${encodeURIComponent(name)}`, { method: "DELETE" }); refresh(); } catch { /* */ }
  }, [refresh, t.projects]);

  return (
    <ScrollArea className="h-full">
      <div className="p-8 max-w-3xl mx-auto">
        {projects.length > 0 && (
          <div className="flex gap-3 mb-8">
            {[
              { label: t.tabs.projects, value: aggregate.projects, accent: "text-primary" },
              { label: t.projects.nodesLabel, value: aggregate.nodes, accent: "text-foreground" },
              { label: t.projects.edgesLabel, value: aggregate.edges, accent: "text-foreground" },
            ].map((s) => (
              <div key={s.label} className="flex-1 rounded-xl surface-panel p-4">
                <p className="text-[10px] text-muted-foreground uppercase tracking-[0.14em] mb-1.5">{s.label}</p>
                <p className={`text-[24px] font-semibold tabular-nums tracking-tight ${s.accent}`}>
                  {s.value.toLocaleString()}
                </p>
              </div>
            ))}
          </div>
        )}

        {indexing && <IndexProgress onDone={() => { setIndexing(false); refresh(); }} />}

        <div className="flex items-center justify-between mb-6">
          <h2 className="text-[17px] font-semibold text-foreground tracking-tight">{t.projects.indexedProjects}</h2>
          <div className="flex items-center gap-2">
            <button
              onClick={() => setShowModal(true)}
              className="px-3.5 py-2 rounded-lg bg-primary text-primary-foreground text-[12px] font-semibold transition-all hover:brightness-110"
            >
              + {t.index.newIndex}
            </button>
            <button
              onClick={refresh}
              disabled={loading}
              className="px-3 py-2 rounded-lg border border-border/50 bg-white/[0.03] hover:bg-white/[0.06] text-[12px] text-muted-foreground font-medium transition-all disabled:opacity-30"
            >
              {loading ? "..." : t.common.refresh}
            </button>
          </div>
        </div>

        {error && (
          <div className="rounded-xl border border-destructive/20 bg-destructive/5 p-4 mb-6">
            <p className="text-destructive text-[13px]">{error}</p>
          </div>
        )}

        {!loading && projects.length === 0 && !error && (
          <div className="anim-empty-rise text-center py-24 px-6 rounded-2xl surface-panel">
            <div className="mx-auto mb-5 flex h-12 w-12 items-center justify-center rounded-full bg-accent/15">
              <span className="h-2.5 w-2.5 rounded-full bg-accent shadow-[0_0_16px_rgba(232,184,109,0.55)]" />
            </div>
            <h3 className="text-[18px] font-semibold text-foreground mb-2">{t.projects.noIndexedProjects}</h3>
            <p className="text-[13px] text-muted-foreground max-w-sm mx-auto mb-6 leading-relaxed">
              {t.projects.emptyHint}
            </p>
            <button
              onClick={() => setShowModal(true)}
              className="px-5 py-2.5 rounded-lg bg-accent text-accent-foreground text-[13px] font-semibold transition-all hover:brightness-110"
            >
              {t.projects.indexFirstRepository}
            </button>
          </div>
        )}

        <div className="space-y-3">
          {projects.map((p) => {
            const { nodes: totalNodes, edges: totalEdges } = projectCounts(p);
            const title = displayName(p.project);
            return (
              <div key={p.project.name} className="rounded-xl surface-panel hover:border-primary/25 transition-all p-5">
                <div className="flex items-start justify-between gap-3 mb-3">
                  <div className="min-w-0 flex items-start gap-3">
                    <div className="mt-1.5"><HealthDot name={p.project.name} /></div>
                    <div className="min-w-0">
                      <h3 className="text-[15px] font-semibold text-foreground mb-0.5 truncate">{title}</h3>
                      <p className="text-[11px] text-muted-foreground/80 font-mono truncate">{p.project.root_path}</p>
                      {p.project.branch && (
                        <p className="text-[10px] text-primary/70 font-mono mt-1">{p.project.branch}</p>
                      )}
                    </div>
                  </div>
                  <div className="flex items-center gap-1.5 shrink-0">
                    <AdrButton project={p.project.name} />
                    <DocsButton project={p.project.name} />
                    <button
                      onClick={() => onSelectProject(p.project.name)}
                      className="px-3 py-1.5 rounded-lg bg-primary/15 hover:bg-primary/25 text-primary text-[12px] font-semibold transition-all"
                    >
                      {t.projects.viewGraph}
                    </button>
                    <button
                      onClick={() => deleteProject(p.project.name)}
                      className="px-2 py-1.5 rounded-lg hover:bg-destructive/10 text-muted-foreground/40 hover:text-destructive text-[12px] transition-all"
                      title={t.projects.deleteTitle}
                    >
                      ✕
                    </button>
                  </div>
                </div>
                <div className="flex gap-6 text-[12px] text-muted-foreground mb-3 pl-[22px]">
                  <span>
                    <strong className="text-foreground tabular-nums">{totalNodes.toLocaleString()}</strong>{" "}
                    {t.projects.nodes}
                  </span>
                  <span>
                    <strong className="text-foreground tabular-nums">{totalEdges.toLocaleString()}</strong>{" "}
                    {t.projects.edges}
                  </span>
                </div>
                {p.schema?.node_labels && p.schema.node_labels.length > 0 && (
                  <div className="flex flex-wrap gap-1 pl-[22px]">
                    {p.schema.node_labels.map((l) => (
                      <span
                        key={l.label}
                        className="inline-flex items-center gap-1 px-1.5 py-[2px] rounded-md text-[10px] font-medium"
                        style={{ backgroundColor: colorForLabel(l.label) + "14", color: colorForLabel(l.label) + "cc" }}
                      >
                        <span className="w-[4px] h-[4px] rounded-full" style={{ backgroundColor: colorForLabel(l.label) }} />
                        {l.label} {l.count.toLocaleString()}
                      </span>
                    ))}
                  </div>
                )}
              </div>
            );
          })}
        </div>
      </div>
      {showModal && <CreateIndexModal onClose={() => setShowModal(false)} onCreated={() => { setIndexing(true); refresh(); }} />}
    </ScrollArea>
  );
}
