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
// What is not there: request.cf (where a request came from), so a quick match pairs by waiting time alone, and
// the timer that measures the relay's use (the Worker's `scheduled`). Room codes, lobbies and spectators need
// neither.
//
// Deploy with JWT verification off (the game sends no Supabase key), this file as index.ts and ../worker.js beside
// it as worker.js. The game: BT3_MATCH_URL=https://<project>.supabase.co/functions/v1/<name>
// (The copy this fork's game asks by default was deployed with the import below naming worker.js at one commit of
// the repository, https://raw.githubusercontent.com/<owner>/<repository>/<commit>/port/matchmaking/worker.js,
// instead of a second file: a change to worker.js reaches it only when it is deployed again.)
import postgres from "npm:postgres@3.4.5";
import worker from "./worker.js";

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

Deno.serve((request: Request) => {
    const url = new URL(request.url);
    const at = url.pathname.indexOf("/v1/");
    url.pathname = at >= 0 ? url.pathname.slice(at) : "/";
    const headers = new Headers(request.headers);
    headers.set("cf-connecting-ip", (headers.get("x-forwarded-for") ?? "").split(",")[0].trim());
    return worker.fetch(new Request(url, { method: request.method, headers, body: request.body }), env);
});
