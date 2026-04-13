# AGENTS.md

## Cursor Cloud specific instructions

### Repository overview

Multi-language learning repository with standalone code samples in TypeScript, Python, Rust, C, C++, C#, Java, and Kotlin. No external services, databases, or APIs required. Each language folder is fully independent.

### Prerequisites

- **Node.js** >= 18 and **npm** >= 9 (TypeScript project)
- **Python** 3.8+ (Python samples)
- **Rust** toolchain / `cargo` (Rust samples)
- C/C++/C#/Java/Kotlin compilers are optional (not part of the npm-based workflow)

### Key commands

All npm scripts are defined in `package.json`. Reference that file for the full list. Summary:

| Task | Command |
|---|---|
| Install deps | `npm install` |
| Build TypeScript | `npm run build` |
| Dev mode (watch) | `npm run dev` |
| Run TS hello world | `npm run ts:run-hello` |
| Start frontend server (port 3000) | `npm run ts:serve` |
| Lint (ESLint) | `npm run lint` |
| Format (Prettier) | `npx prettier --check "typescript/**/*.ts"` |
| Run Python sample | `python3 python/hello_world.py` |
| Run Rust sample | `cd rust/hello_world && rustc src/main.rs -o hello_world && ./hello_world` |
| Full setup | `npm run setup` |

### Known issues (pre-existing in the repo)

1. **ESLint config missing**: ESLint 9 requires `eslint.config.js` but none exists in this repo. `npm run lint` will fail with a config error. This is a pre-existing issue.
2. **TypeScript build errors**: `npm run build` (which runs `tsc --build`) fails due to type errors in `modules/UIComponents.ts` and related files. Individual files like `hello_world.ts` compile fine with `npx tsc hello_world.ts`.
3. **Rust Cargo.toml missing**: The `rust/hello_world/` directory has no `Cargo.toml`. Compile directly with `rustc src/main.rs -o hello_world`.
4. **node_modules/.bin permissions**: After `npm install`, binaries in `node_modules/.bin/` may lack execute permission. Run `chmod +x node_modules/.bin/*` to fix.

### Frontend demo

The TypeScript frontend demo is served at `http://localhost:3000/frontend-demo.html` via `npm run ts:serve`. It is a static HTML page that imports TypeScript-compiled JS modules.
