import { useCallback, useEffect, useState } from "react";
import { GraphTab } from "./components/GraphTab";
import { StatsTab } from "./components/StatsTab";
import { ControlTab } from "./components/ControlTab";
import type { TabId } from "./lib/types";
import { useUiMessages } from "./lib/i18n";

const TAB_IDS: TabId[] = ["graph", "stats", "control"];

interface RouteState {
  tab: TabId;
  project: string | null;
}

function readRoute(): RouteState {
  const params = new URLSearchParams(window.location.search);
  const rawTab = params.get("tab");
  const tab = TAB_IDS.includes(rawTab as TabId) ? (rawTab as TabId) : "stats";
  const project = params.get("project");
  return { tab, project: project ? project : null };
}

function routeUrl(tab: TabId, project: string | null): string {
  const params = new URLSearchParams();
  params.set("tab", tab);
  if (project) params.set("project", project);
  return `${window.location.pathname}?${params.toString()}${window.location.hash}`;
}

export function App() {
  const t = useUiMessages();
  const [route, setRoute] = useState<RouteState>(readRoute);
  const { tab: activeTab, project: selectedProject } = route;

  useEffect(() => {
    const initial = readRoute();
    window.history.replaceState(null, "", routeUrl(initial.tab, initial.project));
  }, []);

  useEffect(() => {
    const onPopState = () => setRoute(readRoute());
    window.addEventListener("popstate", onPopState);
    return () => window.removeEventListener("popstate", onPopState);
  }, []);

  const navigate = useCallback((tab: TabId, project: string | null) => {
    const url = routeUrl(tab, project);
    const current = `${window.location.pathname}${window.location.search}${window.location.hash}`;
    if (url === current) return;
    window.history.pushState(null, "", url);
    setRoute({ tab, project });
  }, []);

  const tabs: { id: TabId; label: string }[] = [
    { id: "graph", label: t.tabs.graph },
    { id: "stats", label: t.tabs.projects },
    { id: "control", label: t.tabs.control },
  ];

  return (
    <div className="app-shell h-screen flex flex-col text-foreground">
      <header className="relative flex items-center justify-between px-6 h-14 shrink-0 border-b border-border/60 bg-[#0a1018]/75 backdrop-blur-xl">
        <div className="pointer-events-none absolute inset-x-0 bottom-0 h-px bg-gradient-to-r from-transparent via-primary/40 to-accent/30" />
        <div className="flex items-center gap-8">
          <div className="anim-brand-in flex items-center gap-3">
            <span className="relative flex h-2.5 w-2.5">
              <span className="absolute inset-0 rounded-full bg-accent/70 blur-[3px]" />
              <span className="relative h-2.5 w-2.5 rounded-full bg-accent" />
            </span>
            <div className="leading-none">
              <span className="block text-[15px] font-semibold tracking-tight text-foreground">
                Project Mind
              </span>
              <span className="block text-[9px] uppercase tracking-[0.18em] text-muted-foreground mt-0.5">
                Knowledge graph
              </span>
            </div>
          </div>

          <nav className="flex items-center gap-1 p-1 rounded-xl bg-white/[0.03] border border-border/40">
            {tabs.map((tab) => {
              const disabled = tab.id === "graph" && !selectedProject;
              const active = activeTab === tab.id;
              return (
                <button
                  key={tab.id}
                  onClick={() => navigate(tab.id, tab.id === "stats" ? null : selectedProject)}
                  disabled={disabled}
                  title={disabled ? "Select a project first" : undefined}
                  className={`px-3.5 py-1.5 rounded-lg text-[12px] font-medium transition-all ${
                    disabled
                      ? "text-muted-foreground/25 cursor-not-allowed"
                      : active
                        ? "bg-primary/20 text-primary shadow-[0_0_20px_rgba(45,212,191,0.12)]"
                        : "text-muted-foreground hover:text-foreground hover:bg-white/[0.04]"
                  }`}
                >
                  {tab.label}
                </button>
              );
            })}
          </nav>
        </div>

        {selectedProject && (
          <div className="flex items-center gap-2 px-3 py-1.5 rounded-xl surface-panel">
            <span className="text-[10px] text-muted-foreground uppercase tracking-wider">
              {t.graph.selectedLabel}
            </span>
            <span className="text-[11px] text-primary font-mono truncate max-w-[280px]">
              {selectedProject}
            </span>
            <button
              onClick={() => navigate("stats", null)}
              className="text-muted-foreground/50 hover:text-foreground text-[13px] ml-0.5 transition-colors"
              aria-label="Clear project"
            >
              ×
            </button>
          </div>
        )}
      </header>

      <main className="flex-1 min-h-0">
        {activeTab === "graph" ? (
          <GraphTab project={selectedProject} />
        ) : activeTab === "control" ? (
          <ControlTab />
        ) : (
          <StatsTab onSelectProject={(p) => navigate("graph", p)} />
        )}
      </main>
    </div>
  );
}
