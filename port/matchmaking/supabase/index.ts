// The matchmaking Worker (../worker.js) run as a Supabase Edge Function, with the project's Postgres in place of
// Cloudflare's D1. The Worker's code is used as it is: this file only gives it what Cloudflare gave it.
//
//   - the database: `env.DB` with D1's prepare / bind / first / all / run, over Postgres (toPostgres below turns
//     the Worker's SQLite statements into Postgres ones; the tables: schema.pg.sql)
//   - the caller's address: the header cf-connecting-ip, from x-forwarded-for's first entry
//   - the path: /functions/v1/<name>/v1/<what> arrives as /<name>/v1/<what>; the Worker is given /v1/<what>
//   - the settings: the function's secrets (BUILD_KEYS, REQUIRE_KEY, MIN_RELEASE, TURN_KEY_ID, ...), as the Worker's
//     variables and secrets
//
// The fork's own routes, answered here and not by the Worker: GET /squads (the groups and their squads, a line each,
// tab-separated), POST /v1/result (a finished tournament, from its owner's game), GET /ranking (a page) and
// /ranking.json. Anyone who knows the address can post a result: the limits are its shape and thirty an hour from
// one address.
//
// What is not there: request.cf (where a request came from), so a quick match pairs by waiting time alone, and
// the timer that measures the relay's use (the Worker's `scheduled`). Room codes, lobbies and spectators need
// neither.
//
// Deploy with JWT verification off (the game sends no Supabase key). The copy this fork's game asks by default is
// deployed as one small entry file that imports this file from the repository at one commit
// (https://raw.githubusercontent.com/<owner>/<repository>/<commit>/port/matchmaking/supabase/index.ts, which finds
// ../worker.js beside it there): a change here or to worker.js reaches it only when the entry names a newer commit.
// The game: BT3_MATCH_URL=https://<project>.supabase.co/functions/v1/<name>
import postgres from "npm:postgres@3.4.5";
import worker from "../worker.js";

// SQLite to Postgres, for the statements the Worker has: `?` to $1.., the tables in their schema (bt3), and the
// counter's own column named with its table (Postgres does not know which `n` is meant in ON CONFLICT ... SET n =
// n + 1).
export function toPostgres(sql: string): string {
    let i = 0;
    return sql.replace(/\?/g, () => "$" + ++i)
        .replace(/\b(FROM|INTO|UPDATE) (rooms|used|watchers)\b/g, "$1 bt3.$2")
        .replace(/DO UPDATE SET n = n \+ 1/, "DO UPDATE SET n = used.n + 1");
}

// bigint (the times, the counters, COUNT(*)) as numbers, as D1 gives them: they are far below 2^53.
const sql = postgres(Deno.env.get("SUPABASE_DB_URL")!, {
    prepare: false, // (the pooler does not keep prepared statements)
    max: 2,
    types: { bigint: { to: 20, from: [20], parse: (v: string) => Number(v), serialize: (v: number) => String(v) } },
});

function statement(text: string, args: unknown[]) {
    const run = () => sql.unsafe(toPostgres(text), args as never[]);
    return {
        bind: (...more: unknown[]) => statement(text, more),
        first: async () => (await run())[0] ?? null,
        all: async () => ({ results: [...(await run())] }),
        run: async () => ({ meta: { changes: (await run()).count } }),
    };
}

const env = new Proxy({ DB: { prepare: (text: string) => statement(text, []) } } as Record<string, unknown>, {
    get: (own, name: string) => (name in own ? own[name] : Deno.env.get(name)),
});

// ---- the fork's own: squads, a tournament's result, the ranking (tables bt3.groups, bt3.squads, bt3.results) ----
const clean = (v: unknown, max: number) => String(v ?? "").replace(/[\u0000-\u001f\u007f]/g, "").slice(0, max);
const tagOf = (name: string) => (/^\[([A-Z0-9]{1,4})\] /.exec(name) ?? ["", ""])[1];
const esc = (v: string) => v.replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]!));

async function ranking() {
    const rows = await sql`SELECT champion, champion_tag, matches FROM bt3.results ORDER BY id DESC LIMIT 500`;
    const players = new Map<string, { name: string; titles: number; wins: number; losses: number }>();
    const squads = new Map<string, { tag: string; titles: number; wins: number; losses: number }>();
    const player = (n: string) => players.get(n) ?? players.set(n, { name: n, titles: 0, wins: 0, losses: 0 }).get(n)!;
    const squad = (t: string) => squads.get(t) ?? squads.set(t, { tag: t, titles: 0, wins: 0, losses: 0 }).get(t)!;
    for (const r of rows) {
        player(r.champion).titles++;
        if (r.champion_tag) squad(r.champion_tag).titles++;
        for (const m of Array.isArray(r.matches) ? r.matches : []) {
            const w = clean(m.w, 20), l = clean(m.w === m.a ? m.b : m.a, 20);
            if (!w || !l) continue;
            player(w).wins++;
            player(l).losses++;
            if (tagOf(w) && tagOf(l) && tagOf(w) !== tagOf(l)) { // (a squad's score: its wins over other squads)
                squad(tagOf(w)).wins++;
                squad(tagOf(l)).losses++;
            }
        }
    }
    const order = <T extends { titles: number; wins: number; losses: number }>(m: Map<string, T>) =>
        [...m.values()].sort((x, y) => y.titles - x.titles || y.wins - x.wins || x.losses - y.losses);
    return { tournaments: rows.length, players: order(players), squads: order(squads) };
}

async function own(request: Request, path: string): Promise<Response | null> {
    if (request.method === "GET" && path.endsWith("/squads")) { // one line per squad: group, its name, tag, name, colour
        const rows = await sql`SELECT g.id, g.name AS gname, s.tag, s.name, s.color FROM bt3.squads s JOIN bt3.groups g ON g.id = s.group_id ORDER BY g.ord, g.id, s.ord, s.tag`;
        const text = rows.map((r) => [r.id, r.gname, r.tag, r.name, r.color].map((v) => clean(v, 24).replace(/\t/g, " ")).join("\t")).join("\n");
        return new Response(text + "\n", { headers: { "content-type": "text/plain; charset=utf-8" } });
    }
    if (request.method === "POST" && path.endsWith("/v1/result")) { // a finished tournament, from its owner's game
        const raw = await request.text();
        const ip = (request.headers.get("x-forwarded-for") ?? "").split(",")[0].trim(), now = Math.floor(Date.now() / 1000);
        let body: Record<string, unknown>;
        try { body = JSON.parse(raw); } catch { return Response.json({ error: "bad request" }, { status: 400 }); }
        const matches = Array.isArray(body.matches) ? body.matches.slice(0, 7).map((m) => ({ a: clean(m?.a, 20), b: clean(m?.b, 20), w: clean(m?.w, 20) })) : [];
        const champion = clean(body.champion, 20), entrants = Number(body.entrants);
        if (raw.length > 4096 || !champion || !Number.isInteger(entrants) || entrants < 2 || entrants > 8 || matches.length < 1 ||
            matches.some((m) => !m.a || !m.b || (m.w !== m.a && m.w !== m.b))) {
            return Response.json({ error: "bad request" }, { status: 400 });
        }
        const [{ n }] = await sql`SELECT COUNT(*) AS n FROM bt3.results WHERE ip = ${ip} AND made > ${now - 3600}`;
        if (n >= 30) return Response.json({ error: "busy" }, { status: 429 }); // (one address: thirty tournaments an hour)
        await sql`INSERT INTO bt3.results (made, ip, grp, champion, champion_tag, entrants, matches) VALUES (${now}, ${ip}, ${clean(body.grp, 12)}, ${champion}, ${tagOf(champion)}, ${entrants}, ${sql.json(matches)})`;
        return Response.json({ ok: true });
    }
    if (request.method === "GET" && path.endsWith("/ranking.json")) {
        return Response.json(await ranking());
    }
    if (request.method === "GET" && path.endsWith("/ranking")) {
        const r = await ranking();
        const table = (head: string, rows: string[][]) => `<table><tr>${["#", head, "Títulos", "Vitórias", "Derrotas"].map((h) => `<th>${h}</th>`).join("")}</tr>` +
            rows.map((c, i) => `<tr><td>${i + 1}</td>${c.map((v) => `<td>${esc(v)}</td>`).join("")}</tr>`).join("") + "</table>";
        const html = `<!doctype html><html lang="pt-BR"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Ranking da Rinha</title>
<style>body{font-family:Poppins,system-ui,sans-serif;background:#002c56;color:#fff;margin:0;padding:24px 16px}main{max-width:720px;margin:auto}h1{margin:0 0 4px}h2{margin:28px 0 8px;color:#97ccfd;font-size:1rem;text-transform:uppercase}p{color:#97ccfd;margin:0}
table{width:100%;border-collapse:collapse;background:#001e3c;border-radius:14px;overflow:hidden}th,td{padding:10px 12px;text-align:left}th{background:#0062ab;font-size:.8rem}td:nth-child(n+3),th:nth-child(n+3){text-align:right}tr+tr td{border-top:1px solid #0b3a66}</style>
<main><h1>Ranking da Rinha</h1><p>${r.tournaments} rinha${r.tournaments === 1 ? "" : "s"} registrada${r.tournaments === 1 ? "" : "s"}</p>
<h2>Squads</h2>${table("Squad", r.squads.map((s) => [s.tag, String(s.titles), String(s.wins), String(s.losses)]))}
<h2>Viajantes</h2>${table("Viajante", r.players.slice(0, 100).map((p) => [p.name, String(p.titles), String(p.wins), String(p.losses)]))}</main></html>`;
        return new Response(html, { headers: { "content-type": "text/html; charset=utf-8" } });
    }
    return null;
}

Deno.serve(async (request: Request) => {
    const url = new URL(request.url);
    const mine = await own(request, url.pathname).catch((e) => Response.json({ error: "failed", why: String(e).slice(0, 200) }, { status: 500 }));
    if (mine !== null) {
        return mine;
    }
    const at = url.pathname.indexOf("/v1/");
    url.pathname = at >= 0 ? url.pathname.slice(at) : "/";
    const headers = new Headers(request.headers);
    headers.set("cf-connecting-ip", (headers.get("x-forwarded-for") ?? "").split(",")[0].trim());
    return worker.fetch(new Request(url, { method: request.method, headers, body: request.body }), env);
});
