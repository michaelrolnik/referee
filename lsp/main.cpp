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

#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>

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
