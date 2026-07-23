/*
 *  MIT License — Copyright (c) 2022-2026 Michael Rolnik
 *
 *  referee-lsp — a Language Server for REF.
 *
 *  Speaks LSP (JSON-RPC over stdio, Content-Length framed) and reuses the referee
 *  front-end (`Referee::diagnose`) to publish live parse + type diagnostics as you
 *  edit. ANTLR has no incremental parse, but REF specs are tiny, so each change
 *  re-parses the whole document — full text sync, no incremental machinery.
 *
 *  Today: initialize / shutdown, full-document sync (didOpen/didChange/didClose),
 *  and pushed diagnostics. Hover / completion / document symbols are natural
 *  follow-ups on the same AST.
 */
#include "referee.hpp"

#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"

#include <cctype>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ── LSP framing ─────────────────────────────────────────────────────────────

std::optional<std::string> readMessage()
{
    std::size_t contentLength = 0;
    std::string line;
    while (std::getline(std::cin, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;                          // blank line ends the headers
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
        if (key == "Content-Length") contentLength = std::stoul(val);
    }
    if (contentLength == 0) return std::nullopt;           // EOF, or a message with no body

    std::string body(contentLength, '\0');
    std::cin.read(body.data(), static_cast<std::streamsize>(contentLength));
    if (std::cin.gcount() != static_cast<std::streamsize>(contentLength)) return std::nullopt;
    return body;
}

void writeMessage(const llvm::json::Value& v)
{
    std::string        s;
    llvm::raw_string_ostream os(s);
    os << v;
    os.flush();
    std::cout << "Content-Length: " << s.size() << "\r\n\r\n" << s;
    std::cout.flush();
}

// A JSON-RPC response to a request `id`.
void reply(const llvm::json::Value& id, llvm::json::Value result)
{
    writeMessage(llvm::json::Object{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}});
}

// A JSON-RPC error response to a request `id`.
void replyError(const llvm::json::Value& id, int code, llvm::StringRef message)
{
    writeMessage(llvm::json::Object{{"jsonrpc", "2.0"}, {"id", id},
        {"error", llvm::json::Object{{"code", code}, {"message", message}}}});
}

// A JSON-RPC notification (no id).
void notify(llvm::StringRef method, llvm::json::Value params)
{
    writeMessage(llvm::json::Object{{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}});
}

// ── URI → filesystem path (so imports resolve against the file's directory) ──

std::string uriToPath(llvm::StringRef uri)
{
    std::string s = uri.str();
    const std::string prefix = "file://";
    if (s.rfind(prefix, 0) == 0) s = s.substr(prefix.size());
    // percent-decode
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) { out.push_back(static_cast<char>(hi * 16 + lo)); i += 2; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}

// Filesystem path -> file:// URI (percent-encode the few chars uriToPath decodes).
std::string pathToUri(const std::string& path)
{
    std::string out = "file://";
    for (char c : path)
    {
        if      (c == ' ') out += "%20";
        else if (c == '%') out += "%25";
        else               out.push_back(c);
    }
    return out;
}

// ── Diagnostics ──────────────────────────────────────────────────────────────

void publishDiagnostics(llvm::StringRef uri, llvm::StringRef text)
{
    std::istringstream is(text.str());
    std::vector<Referee::Diagnostic> diags;
    try
    {
        diags = Referee::diagnose(is, uriToPath(uri), /*includePaths*/ {});
    }
    catch (...)
    {
        // diagnose() is declared non-throwing, but never let the server die on a doc.
    }

    llvm::json::Array items;
    for (auto const& d : diags)
    {
        items.push_back(llvm::json::Object{
            {"range", llvm::json::Object{
                {"start", llvm::json::Object{{"line", d.startLine}, {"character", d.startCol}}},
                {"end",   llvm::json::Object{{"line", d.endLine},   {"character", d.endCol}}},
            }},
            {"severity", 1},                 // 1 = Error
            {"source", "referee"},
            {"message", d.message},
        });
    }
    notify("textDocument/publishDiagnostics",
           llvm::json::Object{{"uri", uri}, {"diagnostics", std::move(items)}});
}

// ── Open documents ───────────────────────────────────────────────────────────
// Completion needs the current buffer text at an arbitrary caret, so unlike
// diagnostics (which run off the text handed to the notification) it is kept here.
std::map<std::string, std::string> g_docs;

// ── Completion: the `.` after a signal → its struct fields / enum cases. ──────
void handleCompletion(const llvm::json::Value& id, llvm::json::Object* params)
{
    llvm::json::Array items;
    auto emit = [&]() {
        reply(id, llvm::json::Object{{"isIncomplete", false}, {"items", std::move(items)}});
    };

    if (!params) { emit(); return; }
    auto* doc = params->getObject("textDocument");
    auto* pos = params->getObject("position");
    if (!doc || !pos) { emit(); return; }

    std::string uri = doc->getString("uri").value_or("").str();
    auto it = g_docs.find(uri);
    if (it == g_docs.end()) { emit(); return; }

    unsigned line = static_cast<unsigned>(pos->getInteger("line").value_or(0));
    unsigned chr  = static_cast<unsigned>(pos->getInteger("character").value_or(0));

    std::vector<Referee::Completion> cands;
    try   { std::istringstream is(it->second);
            cands = Referee::complete(is, uriToPath(uri), /*includePaths*/ {}, line, chr); }
    catch (...) { /* never let completion crash the server */ }

    for (auto const& c : cands)
        items.push_back(llvm::json::Object{{"label", c.label}, {"kind", c.kind}});
    emit();
}

// ── Hover: the declaration of the symbol under the caret. ─────────────────────
void handleHover(const llvm::json::Value& id, llvm::json::Object* params)
{
    std::string markdown;
    if (params)
        if (auto* doc = params->getObject("textDocument"))
            if (auto* pos = params->getObject("position"))
            {
                std::string uri = doc->getString("uri").value_or("").str();
                auto it = g_docs.find(uri);
                if (it != g_docs.end())
                {
                    unsigned line = static_cast<unsigned>(pos->getInteger("line").value_or(0));
                    unsigned chr  = static_cast<unsigned>(pos->getInteger("character").value_or(0));
                    try   { std::istringstream is(it->second);
                            markdown = Referee::hover(is, uriToPath(uri), /*includePaths*/ {}, line, chr); }
                    catch (...) { /* never let hover crash the server */ }
                }
            }

    if (markdown.empty()) { reply(id, nullptr); return; }   // null = no hover here
    reply(id, llvm::json::Object{
        {"contents", llvm::json::Object{{"kind", "markdown"}, {"value", markdown}}}});
}

// ── Go-to-definition: jump to the declaration of the symbol under the caret. ──
void handleDefinition(const llvm::json::Value& id, llvm::json::Object* params)
{
    Referee::Definition def;
    std::string         uri;
    if (params)
        if (auto* doc = params->getObject("textDocument"))
            if (auto* pos = params->getObject("position"))
            {
                uri = doc->getString("uri").value_or("").str();
                auto it = g_docs.find(uri);
                if (it != g_docs.end())
                {
                    unsigned line = static_cast<unsigned>(pos->getInteger("line").value_or(0));
                    unsigned chr  = static_cast<unsigned>(pos->getInteger("character").value_or(0));
                    try   { std::istringstream is(it->second);
                            def = Referee::define(is, uriToPath(uri), /*includePaths*/ {}, line, chr); }
                    catch (...) { /* never let go-to-def crash the server */ }
                }
            }

    if (!def.found) { reply(id, nullptr); return; }         // null = no definition
    // The declaration may live in an imported file; use its path when given.
    std::string targetUri = def.file.empty() ? uri : pathToUri(def.file);
    reply(id, llvm::json::Object{
        {"uri", targetUri},
        {"range", llvm::json::Object{
            {"start", llvm::json::Object{{"line", def.line}, {"character", def.startCol}}},
            {"end",   llvm::json::Object{{"line", def.line}, {"character", def.endCol}}},
        }}});
}

// ── Document symbols: the outline (declarations, with members nested). ────────
llvm::json::Value symbolToJson(Referee::Symbol const& s)
{
    llvm::json::Array children;
    for (auto const& c : s.children)
        children.push_back(symbolToJson(c));

    llvm::json::Object o{
        {"name", s.name},
        {"kind", s.kind},
        {"range", llvm::json::Object{
            {"start", llvm::json::Object{{"line", s.line},    {"character", s.startCol}}},
            {"end",   llvm::json::Object{{"line", s.endLine}, {"character", s.endChar}}},
        }},
        {"selectionRange", llvm::json::Object{
            {"start", llvm::json::Object{{"line", s.line}, {"character", s.startCol}}},
            {"end",   llvm::json::Object{{"line", s.line}, {"character", s.endCol}}},
        }},
        {"children", std::move(children)},
    };
    if (!s.detail.empty())
        o["detail"] = s.detail;
    return o;
}

// ── Find references: every use of the name under the caret. ──────────────────
void handleReferences(const llvm::json::Value& id, llvm::json::Object* params)
{
    llvm::json::Array out;
    if (params)
        if (auto* doc = params->getObject("textDocument"))
            if (auto* pos = params->getObject("position"))
            {
                std::string uri = doc->getString("uri").value_or("").str();
                auto it = g_docs.find(uri);
                if (it != g_docs.end())
                {
                    unsigned line = static_cast<unsigned>(pos->getInteger("line").value_or(0));
                    unsigned chr  = static_cast<unsigned>(pos->getInteger("character").value_or(0));
                    bool     incl = true;
                    if (auto* ctx = params->getObject("context"))
                        incl = ctx->getBoolean("includeDeclaration").value_or(true);

                    std::vector<Referee::Reference> refs;
                    try   { std::istringstream is(it->second);
                            refs = Referee::references(is, uriToPath(uri), /*includePaths*/ {}, line, chr, incl); }
                    catch (...) { /* never let find-references crash the server */ }

                    for (auto const& rf : refs)
                        out.push_back(llvm::json::Object{
                            {"uri", rf.file.empty() ? uri : pathToUri(rf.file)},
                            {"range", llvm::json::Object{
                                {"start", llvm::json::Object{{"line", rf.line}, {"character", rf.startCol}}},
                                {"end",   llvm::json::Object{{"line", rf.line}, {"character", rf.endCol}}},
                            }}});
                }
            }
    reply(id, std::move(out));
}

// ── Signature help: the parameters of the call the caret is inside. ──────────
void handleSignatureHelp(const llvm::json::Value& id, llvm::json::Object* params)
{
    Referee::SignatureHelp help;
    if (params)
        if (auto* doc = params->getObject("textDocument"))
            if (auto* pos = params->getObject("position"))
            {
                auto it = g_docs.find(doc->getString("uri").value_or("").str());
                if (it != g_docs.end())
                {
                    unsigned line = static_cast<unsigned>(pos->getInteger("line").value_or(0));
                    unsigned chr  = static_cast<unsigned>(pos->getInteger("character").value_or(0));
                    try   { std::istringstream is(it->second);
                            help = Referee::signatureHelp(is, uriToPath(doc->getString("uri").value_or("").str()),
                                                          /*includePaths*/ {}, line, chr); }
                    catch (...) { /* never let signature help crash the server */ }
                }
            }

    if (!help.any) { reply(id, nullptr); return; }

    llvm::json::Array signatures;
    for (auto const& s : help.signatures)
    {
        llvm::json::Array parameters;
        for (auto const& p : s.params)
            parameters.push_back(llvm::json::Object{
                {"label", llvm::json::Array{p.labelStart, p.labelEnd}}});   // [start,end] offsets
        signatures.push_back(llvm::json::Object{
            {"label", s.label},
            {"parameters", std::move(parameters)}});
    }
    reply(id, llvm::json::Object{
        {"signatures", std::move(signatures)},
        {"activeSignature", help.activeSignature},
        {"activeParameter", help.activeParameter}});
}

// ── Rename ───────────────────────────────────────────────────────────────────

// The identifier range under a position, so prepareRename can pre-select it.
struct WordRange { bool found = false; unsigned line = 0, startCol = 0, endCol = 0; };

WordRange wordAtPosition(const std::string& text, unsigned line, unsigned character)
{
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) { if (c == '\n') { lines.push_back(cur); cur.clear(); } else if (c != '\r') cur.push_back(c); }
    lines.push_back(cur);
    if (line >= lines.size()) return {};

    const std::string& L = lines[line];
    auto isW = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    std::size_t n = L.size(), col = std::min<std::size_t>(character, n);
    std::size_t e = col; while (e < n && isW(L[e])) ++e;
    std::size_t b = col; while (b > 0 && isW(L[b - 1])) --b;
    if (b == e) return {};
    return {true, line, static_cast<unsigned>(b), static_cast<unsigned>(e)};
}

// prepareRename: report the range being renamed, or null if the caret isn't on a name.
void handlePrepareRename(const llvm::json::Value& id, llvm::json::Object* params)
{
    if (params)
        if (auto* doc = params->getObject("textDocument"))
            if (auto* pos = params->getObject("position"))
            {
                auto it = g_docs.find(doc->getString("uri").value_or("").str());
                if (it != g_docs.end())
                {
                    unsigned line = static_cast<unsigned>(pos->getInteger("line").value_or(0));
                    unsigned chr  = static_cast<unsigned>(pos->getInteger("character").value_or(0));
                    WordRange w = wordAtPosition(it->second, line, chr);
                    if (w.found)
                    {
                        reply(id, llvm::json::Object{{"range", llvm::json::Object{
                            {"start", llvm::json::Object{{"line", w.line}, {"character", w.startCol}}},
                            {"end",   llvm::json::Object{{"line", w.line}, {"character", w.endCol}}},
                        }}});
                        return;
                    }
                }
            }
    reply(id, nullptr);                     // nothing renameable here
}

// rename: a WorkspaceEdit replacing every occurrence (across imports) with newName.
void handleRename(const llvm::json::Value& id, llvm::json::Object* params)
{
    if (!params) { reply(id, nullptr); return; }
    auto* doc = params->getObject("textDocument");
    auto* pos = params->getObject("position");
    if (!doc || !pos) { reply(id, nullptr); return; }

    std::string uri     = doc->getString("uri").value_or("").str();
    std::string newName = params->getString("newName").value_or("").str();

    auto it = g_docs.find(uri);
    if (it == g_docs.end()) { reply(id, nullptr); return; }

    unsigned line = static_cast<unsigned>(pos->getInteger("line").value_or(0));
    unsigned chr  = static_cast<unsigned>(pos->getInteger("character").value_or(0));

    Referee::RenameResult result;
    try   { std::istringstream is(it->second);
            result = Referee::rename(is, uriToPath(uri), /*includePaths*/ {}, line, chr, newName); }
    catch (...) { /* never let rename crash the server */ }

    if (!result.valid)
    {
        replyError(id, -32602, "'" + newName + "' is not a valid REF identifier");
        return;
    }
    if (result.edits.empty()) { reply(id, nullptr); return; }

    // Group edits by file URI into a WorkspaceEdit.changes map.
    std::map<std::string, llvm::json::Array> byUri;
    for (auto const& e : result.edits)
    {
        std::string u = e.file.empty() ? uri : pathToUri(e.file);
        byUri[u].push_back(llvm::json::Object{
            {"range", llvm::json::Object{
                {"start", llvm::json::Object{{"line", e.line}, {"character", e.startCol}}},
                {"end",   llvm::json::Object{{"line", e.line}, {"character", e.endCol}}},
            }},
            {"newText", newName}});
    }
    llvm::json::Object changes;
    for (auto& [u, edits] : byUri)
        changes[u] = std::move(edits);
    reply(id, llvm::json::Object{{"changes", std::move(changes)}});
}

void handleDocumentSymbol(const llvm::json::Value& id, llvm::json::Object* params)
{
    llvm::json::Array out;
    if (params)
        if (auto* doc = params->getObject("textDocument"))
        {
            std::string uri = doc->getString("uri").value_or("").str();
            auto it = g_docs.find(uri);
            if (it != g_docs.end())
            {
                std::vector<Referee::Symbol> syms;
                try   { std::istringstream is(it->second);
                        syms = Referee::symbols(is, uriToPath(uri), /*includePaths*/ {}); }
                catch (...) { /* never let the outline crash the server */ }
                for (auto const& s : syms)
                    out.push_back(symbolToJson(s));
            }
        }
    reply(id, std::move(out));
}

} // namespace

int main()
{
    std::ios::sync_with_stdio(false);

    while (auto raw = readMessage())
    {
        auto parsed = llvm::json::parse(*raw);
        if (!parsed) { llvm::consumeError(parsed.takeError()); continue; }
        llvm::json::Object* msg = parsed->getAsObject();
        if (!msg) continue;

        llvm::StringRef method = msg->getString("method").value_or("");
        const llvm::json::Value* id = msg->get("id");

        if (method == "initialize")
        {
            reply(*id, llvm::json::Object{
                {"capabilities", llvm::json::Object{
                    {"textDocumentSync", 1},   // 1 = Full (ANTLR re-parses the whole doc anyway)
                    {"completionProvider", llvm::json::Object{
                        {"triggerCharacters", llvm::json::Array{"."}},
                        {"resolveProvider", false},
                    }},
                    {"hoverProvider", true},
                    {"definitionProvider", true},
                    {"documentSymbolProvider", true},
                    {"referencesProvider", true},
                    {"renameProvider", llvm::json::Object{{"prepareProvider", true}}},
                    {"signatureHelpProvider", llvm::json::Object{
                        {"triggerCharacters", llvm::json::Array{"(", ","}},
                    }},
                }},
                {"serverInfo", llvm::json::Object{{"name", "referee-lsp"}, {"version", "0.2.0"}}},
            });
        }
        else if (method == "shutdown")
        {
            if (id) reply(*id, nullptr);
        }
        else if (method == "exit")
        {
            break;
        }
        else if (method == "textDocument/didOpen")
        {
            if (auto* p = msg->getObject("params"))
                if (auto* doc = p->getObject("textDocument"))
                {
                    std::string uri  = doc->getString("uri").value_or("").str();
                    std::string text = doc->getString("text").value_or("").str();
                    g_docs[uri] = text;                 // keep for completion
                    publishDiagnostics(uri, text);
                }
        }
        else if (method == "textDocument/didChange")
        {
            // Full sync: the last content change carries the whole new text.
            if (auto* p = msg->getObject("params"))
            {
                std::string uri;
                if (auto* doc = p->getObject("textDocument")) uri = doc->getString("uri").value_or("").str();
                if (auto* changes = p->getArray("contentChanges"); changes && !changes->empty())
                    if (auto* last = changes->back().getAsObject())
                    {
                        std::string text = last->getString("text").value_or("").str();
                        g_docs[uri] = text;             // keep for completion
                        publishDiagnostics(uri, text);
                    }
            }
        }
        else if (method == "textDocument/completion")
        {
            handleCompletion(*id, msg->getObject("params"));
        }
        else if (method == "textDocument/hover")
        {
            handleHover(*id, msg->getObject("params"));
        }
        else if (method == "textDocument/definition")
        {
            handleDefinition(*id, msg->getObject("params"));
        }
        else if (method == "textDocument/documentSymbol")
        {
            handleDocumentSymbol(*id, msg->getObject("params"));
        }
        else if (method == "textDocument/references")
        {
            handleReferences(*id, msg->getObject("params"));
        }
        else if (method == "textDocument/prepareRename")
        {
            handlePrepareRename(*id, msg->getObject("params"));
        }
        else if (method == "textDocument/rename")
        {
            handleRename(*id, msg->getObject("params"));
        }
        else if (method == "textDocument/signatureHelp")
        {
            handleSignatureHelp(*id, msg->getObject("params"));
        }
        else if (method == "textDocument/didClose")
        {
            // Clear the diagnostics for a closed document, and forget its text.
            if (auto* p = msg->getObject("params"))
                if (auto* doc = p->getObject("textDocument"))
                {
                    std::string uri = doc->getString("uri").value_or("").str();
                    g_docs.erase(uri);
                    notify("textDocument/publishDiagnostics",
                           llvm::json::Object{{"uri", uri}, {"diagnostics", llvm::json::Array{}}});
                }
        }
        // Any request we don't handle but that carries an id must still get a reply,
        // or a strict client blocks. Unknown notifications are ignored.
        else if (id)
        {
            reply(*id, nullptr);
        }
    }

    return 0;
}
