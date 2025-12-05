#include <string>
#include <sstream>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cctype>
#include <functional>
#include "../shared/CslRepresentation.h"
#include "../shared/CslRepr2Csl.h"

namespace CSL {

static std::string htmlEscape(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
        case '&': o << "&amp;"; break;
        case '<': o << "&lt;"; break;
        case '>': o << "&gt;"; break;
        case '"': o << "&quot;"; break;
        case '\'': o << "&#39;"; break;
        default: o << c; break;
        }
    }
    return o.str();
}

static std::string jsonEscape(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::ostringstream o;

    for (unsigned char uc : s) {
        switch (uc) {
        case '\"': o << "\\\""; break;
        case '\\': o << "\\\\"; break;
        case '\b': o << "\\b";  break;
        case '\f': o << "\\f";  break;
        case '\n': o << "\\n";  break;
        case '\r': o << "\\r";  break;
        case '\t': o << "\\t";  break;
        default:
            if (uc < 0x20 || uc == 0x7F) {
                // JSON disallows raw control chars; emit unicode escape
                o << "\\u00" << hex[(uc >> 4) & 0xF] << hex[uc & 0xF];
            } else {
                o << static_cast<char>(uc);
            }
            break;
        }
    }
    return o.str();
}

// Small helpers for graphs
struct GraphNode {
    std::string id;     // stable id, based on pathKey
    std::string label;  // human readable (displayPath / key)
    std::string file;   // target html file (may be empty)
    size_t depth = 0;   // 0 = root / center
};

struct GraphEdge {
    std::string from;   // node id
    std::string to;     // node id
    std::string label;  // key label
};

static std::string buildStructureGraphJson(
    const std::string& schemaName,
    const std::vector<GraphNode>& nodes,
    const std::vector<GraphEdge>& edges
) {
    std::ostringstream json;
    json << '{';
    json << "\"schema\":\"" << jsonEscape(schemaName) << "\",";
    json << "\"nodes\":[";
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (i) json << ',';
        const auto& n = nodes[i];
        json << '{';
        json << "\"id\":\"" << jsonEscape(n.id) << "\",";
        json << "\"label\":\"" << jsonEscape(n.label) << "\",";
        json << "\"file\":\"" << jsonEscape(n.file) << "\",";
        json << "\"depth\":" << n.depth;
        json << '}';
    }
    json << "],\"edges\":[";
    for (size_t i = 0; i < edges.size(); ++i) {
        if (i) json << ',';
        const auto& e = edges[i];
        json << '{';
        json << "\"from\":\"" << jsonEscape(e.from) << "\",";
        json << "\"to\":\"" << jsonEscape(e.to) << "\",";
        json << "\"key\":\"" << jsonEscape(e.label) << "\"";
        json << '}';
    }
    json << "]}";
    return json.str();
}

static bool isIdentifier(const std::string& name) {
    if (name.empty()) return false;
    if (!(((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_'))) return false;
    for (size_t i = 1; i < name.size(); ++i) {
        char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

static std::string quoteIdentifier(const std::string& name) {
    if (name == "*") return name;
    if (isIdentifier(name)) return name;
    std::ostringstream oss;
    oss << '`';
    for (char c : name) {
        if (c == '`' || c == '\\') {
            oss << '\\' << c;
        } else {
            oss << c;
        }
    }
    oss << '`';
    return oss.str();
}

// -------------------- Expr printing (for constraints + annotations) --------------------

static void printAnnotationArgs(const std::vector<std::shared_ptr<Expr>>& args, std::ostream& os);
static void printExpr(const std::shared_ptr<Expr>& expr, std::ostream& os);

static void printAnnotationArgs(const std::vector<std::shared_ptr<Expr>>& args, std::ostream& os) {
    bool first = true;
    for (const auto& a : args) {
        if (!first) os << ", ";
        first = false;
        printExpr(a, os);
    }
}

static void printFunctionCall(const std::shared_ptr<FunctionCallExpr>& f, std::ostream& os) {
    os << f->getFuncName() << '(';
    for (size_t i = 0; i < f->getArgs().size(); ++i) {
        if (i) os << ", ";
        printExpr(f->getArgs()[i], os);
    }
    os << ')';
}

static void printFunctionArg(const std::shared_ptr<FunctionArgExpr>& a, std::ostream& os) {
    const auto& v = a->getValue();
    if (std::holds_alternative<std::shared_ptr<Expr>>(v)) {
        printExpr(std::get<std::shared_ptr<Expr>>(v), os);
    } else {
        const auto& list = std::get<std::vector<std::shared_ptr<Expr>>>(v);
        os << '[';
        for (size_t i = 0; i < list.size(); ++i) {
            if (i) os << ", ";
            printExpr(list[i], os);
        }
        os << ']';
    }
}

static void printExpr(const std::shared_ptr<Expr>& expr, std::ostream& os) {
    if (!expr) return;
    switch (expr->getKind()) {
    case Expr::Kind::BinaryOp: {
        auto b = std::static_pointer_cast<BinaryExpr>(expr);
        printExpr(b->getLHS(), os);
        os << ' ' << b->getOp() << ' ';
        printExpr(b->getRHS(), os);
        break;
    }
    case Expr::Kind::UnaryOp: {
        auto u = std::static_pointer_cast<UnaryExpr>(expr);
        os << u->getOp();
        printExpr(u->getOperand(), os);
        break;
    }
    case Expr::Kind::TernaryOp: {
        auto t = std::static_pointer_cast<TernaryExpr>(expr);
        printExpr(t->getCondition(), os);
        os << " ? ";
        printExpr(t->getTrueExpr(), os);
        os << " : ";
        printExpr(t->getFalseExpr(), os);
        break;
    }
    case Expr::Kind::Literal: {
        auto l = std::static_pointer_cast<LiteralExpr>(expr);
        os << l->getValue();
        break;
    }
    case Expr::Kind::Identifier: {
        auto id = std::static_pointer_cast<IdentifierExpr>(expr);
        os << id->getName();
        break;
    }
    case Expr::Kind::FunctionArg: {
        printFunctionArg(std::static_pointer_cast<FunctionArgExpr>(expr), os);
        break;
    }
    case Expr::Kind::FunctionCall: {
        printFunctionCall(std::static_pointer_cast<FunctionCallExpr>(expr), os);
        break;
    }
    case Expr::Kind::Annotation: {
        auto ae = std::static_pointer_cast<AnnotationExpr>(expr);
        printExpr(ae->getTarget(), os);
        os << ' ' << '@' << ae->getAnnotation()->getName() << '(';
        printAnnotationArgs(ae->getAnnotation()->getArgs(), os);
        os << ')';
        break;
    }
    }
}

static std::string exprToString(const std::shared_ptr<Expr>& e) {
    std::ostringstream os;
    printExpr(e, os);
    return os.str();
}

static std::string renderAnnotationsPlain(const std::vector<std::shared_ptr<Annotation>>& annotations) {
    std::ostringstream os;
    for (size_t i = 0; i < annotations.size(); ++i) {
        const auto& ann = annotations[i];
        if (i) os << ' ';
        os << '@' << ann->getName() << '(';
        printAnnotationArgs(ann->getArgs(), os);
        os << ')';
    }
    return os.str();
}

static std::string renderAnnotationsHtml(const std::vector<std::shared_ptr<Annotation>>& annotations) {
    if (annotations.empty()) return "";
    std::ostringstream out;
    out << "<div class=\"chips\">";
    for (const auto& ann : annotations) {
        std::ostringstream s;
        s << '@' << ann->getName() << '(';
        printAnnotationArgs(ann->getArgs(), s);
        s << ')';
        out << "<span class=\"chip\"><code>" << htmlEscape(s.str()) << "</code></span>";
    }
    out << "</div>";
    return out.str();
}

// -------------------- Types --------------------

static bool isEnumPrimitive(const std::shared_ptr<CSLType>& type) {
    if (!type || type->getKind() != CSLType::Kind::Primitive) return false;
    auto pt = std::static_pointer_cast<PrimitiveType>(type);
    return !pt->getAllowedValues().empty();
}

static std::string typeLabel(const std::shared_ptr<CSLType>& type) {
    switch (type->getKind()) {
    case CSLType::Kind::Primitive: {
        auto pt = std::static_pointer_cast<PrimitiveType>(type);
        const auto& allowed = pt->getAllowedValues();
        if (!allowed.empty()) {
            std::ostringstream os;
            for (size_t i = 0; i < allowed.size(); ++i) {
                if (i) os << " | ";
                os << allowed[i].first;
            }
            return os.str();
        }
        switch (pt->getPrimitive()) {
        case PrimitiveType::Primitive::String: return "string";
        case PrimitiveType::Primitive::Number: return "number";
        case PrimitiveType::Primitive::Boolean: return "boolean";
        case PrimitiveType::Primitive::Datetime: return "datetime";
        case PrimitiveType::Primitive::Duration: return "duration";
        }
        return "primitive";
    }
    case CSLType::Kind::Table: return "table";
    case CSLType::Kind::Array: {
        auto at = std::static_pointer_cast<ArrayType>(type);
        return typeLabel(at->getElementType()) + "[]";
    }
    case CSLType::Kind::Union: {
        auto ut = std::static_pointer_cast<UnionType>(type);
        std::ostringstream os;
        for (size_t i = 0; i < ut->getMemberTypes().size(); ++i) {
            if (i) os << " | ";
            os << typeLabel(ut->getMemberTypes()[i]);
        }
        return os.str();
    }
    case CSLType::Kind::AnyTable: return "any{}";
    case CSLType::Kind::AnyArray: return "any[]";
    case CSLType::Kind::Invalid: return "";
    }
    return "";
}

static size_t countKeys(const std::shared_ptr<TableType>& table) {
    size_t c = table->getExplicitKeys().size();
    if (table->getWildcardKey()) ++c;
    return c;
}

static size_t nestedDepth(const std::shared_ptr<CSLType>& type) {
    switch (type->getKind()) {
    case CSLType::Kind::Table: {
        size_t maxd = 1;
        auto tt = std::static_pointer_cast<TableType>(type);
        for (const auto& kd : tt->getExplicitKeys()) {
            maxd = std::max(maxd, 1 + nestedDepth(kd->getType()));
        }
        if (tt->getWildcardKey()) {
            maxd = std::max(maxd, 1 + nestedDepth(tt->getWildcardKey()->getType()));
        }
        return maxd;
    }
    case CSLType::Kind::Array: {
        auto at = std::static_pointer_cast<ArrayType>(type);
        return 1 + nestedDepth(at->getElementType());
    }
    case CSLType::Kind::Union: {
        size_t maxd = 0;
        auto ut = std::static_pointer_cast<UnionType>(type);
        for (const auto& mt : ut->getMemberTypes()) maxd = std::max(maxd, nestedDepth(mt));
        return maxd;
    }
    default:
        return 0;
    }
}

static std::string siteCss() {
    return R"CSS(
:root{
  --bg:#0b1020;
  --bg2:#0a0f1e;
  --panel:#0f172a;
  --card:#111b33;
  --card2:#0f1930;
  --text:#e6e9f2;
  --muted:#a8b0c3;
  --faint:#7b83a0;
  --border:rgba(255,255,255,.10);
  --border2:rgba(255,255,255,.16);
  --accent:#7c3aed;
  --accent2:#22c55e;
  --warn:#f59e0b;
  --bad:#ef4444;
  --shadow: 0 10px 30px rgba(0,0,0,.35);
  --radius:14px;
  --mono: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", monospace;
  --sans: ui-sans-serif, system-ui, -apple-system, Segoe UI, Roboto, Helvetica, Arial, "Apple Color Emoji", "Segoe UI Emoji";
  --scroll-track:rgba(255,255,255,.06);
  --scroll-thumb:rgba(124,58,237,.45);
  --scroll-thumbHover:rgba(124,58,237,.62);
}

:root:not([data-theme="dark"]){
  --bg:#f6f7fb;
  --bg2:#f6f7fb;
  --panel:#ffffff;
  --card:#ffffff;
  --card2:#fbfcff;
  --text:#111827;
  --muted:#4b5563;
  --faint:#6b7280;
  --border:rgba(17,24,39,.10);
  --border2:rgba(17,24,39,.14);
  --accent:#6d28d9;
  --accent2:#16a34a;
  --warn:#b45309;
  --bad:#dc2626;
  --shadow: 0 10px 26px rgba(17,24,39,.10);
  --scroll-track:rgba(17,24,39,.06);
  --scroll-thumb:rgba(109,40,217,.40);
  --scroll-thumbHover:rgba(109,40,217,.60);
}

*{box-sizing:border-box; scrollbar-width:thin; scrollbar-color:var(--scroll-thumb) var(--scroll-track)}
*::-webkit-scrollbar{ width:10px; height:10px }
*::-webkit-scrollbar-track{ background:var(--scroll-track); border-radius:999px }
*::-webkit-scrollbar-thumb{ background:var(--scroll-thumb); border-radius:999px; border:2px solid transparent; background-clip:content-box }
*::-webkit-scrollbar-thumb:hover{ background:var(--scroll-thumbHover); background-clip:content-box }
*::-webkit-scrollbar-corner{ background:var(--scroll-track) }
html,body{height:100%}
body{
  margin:0;
  background:linear-gradient(180deg,var(--bg2),var(--bg));
  color:var(--text);
  font-family:var(--sans);
  line-height:1.45;
  opacity:1;
  transform:none;
  transition: opacity .20s ease, transform .20s ease;
  will-change: opacity, transform;
}

body.preload{ opacity:0; transform:translateY(6px); }
body.page-ready{ opacity:1; transform:none; }
body.leaving{ opacity:0; transform:translateY(-6px); pointer-events:none; }
@media (prefers-reduced-motion: reduce){
  body{ transition:none !important; }
  body.preload{ opacity:1; transform:none; }
}

a{color:inherit}
a.link{color:var(--accent); text-decoration:none}
a.link:hover{text-decoration:underline}

.topbar{
  position:sticky; top:0; z-index:50;
  display:flex; align-items:center; justify-content:space-between;
  padding:14px 18px;
  background:rgba(15,23,42,.72);
  backdrop-filter: blur(10px);
  border-bottom:1px solid var(--border);
}
:root:not([data-theme="dark"]) .topbar{ background:rgba(255,255,255,.72); }

.brand{
  display:flex; align-items:center; gap:10px;
  min-width:0;
  font-weight:700; letter-spacing:.2px;
}
.brand .dot{
  width:10px; height:10px; border-radius:999px;
  background:linear-gradient(135deg,var(--accent),#0ea5e9);
  box-shadow:0 0 0 4px rgba(124,58,237,.15);
}
.brand small{font-weight:600; color:var(--muted)}

.crumbs{
  display:flex; align-items:center;
  min-width:0;
  overflow:hidden;
  white-space:nowrap;
  font-weight:700;
  letter-spacing:.2px;
}
.crumbs .sep{
  padding:0 6px;
  color:var(--muted);
  font-weight:600;
}
.crumbs .node{
  display:inline-flex;
  align-items:center;
  min-width:0;
}
.crumbs .node[hidden]{
  display:none;
}
.crumb{
  display:inline-flex;
  align-items:center;
  padding:2px 6px;
  border-radius:10px;
  text-decoration:none;
  min-width:0;
}
.crumb:hover{ background:rgba(124,58,237,.10); }
.crumb.current{ background:transparent; }
.crumbtxt{
  display:inline-block;
  max-width: 320px;
  overflow:hidden;
  text-overflow:ellipsis;
  vertical-align:bottom;
}

.crumbs button.crumb{
  border:0;
  background:transparent;
  color:inherit;
  font:inherit;
  line-height:inherit;
  cursor:pointer;
}
.crumbs button.crumb:focus{ outline:none; }
.crumbs button.crumb:focus-visible{ box-shadow:0 0 0 4px rgba(124,58,237,.20); }

.ellipsis .crumb{ color:var(--muted); cursor:pointer; }
.ellipsis .crumb:hover{ background:rgba(124,58,237,.10); }

/* Popover menu: fixed so it won't be clipped by .crumbs { overflow:hidden } */
.ellmenu{
  position:fixed;
  top:0;
  left:0;
  z-index:200;
  min-width:220px;
  max-width: min(520px, 80vw);
  max-height: min(320px, 60vh);
  overflow:auto;
  padding:6px;
  border:1px solid var(--border2);
  border-radius:12px;
  background:var(--panel);
  box-shadow:var(--shadow);
}
.ellitem{
  display:flex;
  align-items:center;
  gap:10px;
  padding:8px 10px;
  border-radius:10px;
  text-decoration:none;
  color:var(--text);
  white-space:nowrap;
}
.ellitem:hover{ background:rgba(124,58,237,.10); }
.ellitem:focus{ outline:none; }
.ellitem:focus-visible{ box-shadow:0 0 0 4px rgba(124,58,237,.22); }
.ellitem .muted{ color:var(--muted); font-weight:600; }

.topbar .actions{display:flex; align-items:center; gap:10px}
.iconbtn{
  display:inline-flex; align-items:center; gap:8px;
  border:1px solid var(--border);
  background:linear-gradient(180deg,rgba(255,255,255,.06),rgba(255,255,255,.02));
  color:var(--text);
  padding:8px 10px;
  border-radius:10px;
  cursor:pointer;
  user-select:none;
  font-size:13px;
}
:root:not([data-theme="dark"]) .iconbtn{ background:linear-gradient(180deg,rgba(17,24,39,.04),rgba(17,24,39,.02)); }
.iconbtn:hover{border-color:var(--border2)}
.iconbtn.copied{outline:2px solid rgba(34,197,94,.35); border-color:rgba(34,197,94,.45)}
.iconbtn .kbd{ font-family:var(--mono); font-size:12px; color:var(--muted); }

.app{
  display:grid;
  grid-template-columns: 280px 1fr;
  gap:14px;
  padding:14px;
  max-width:1280px;
  margin:0 auto;
}

.sidebar{
  position:sticky; top:76px;
  height:calc(100vh - 90px);
  overflow:auto;
  padding:12px;
  border:1px solid var(--border);
  border-radius:var(--radius);
  background:var(--panel);
  box-shadow:var(--shadow);
}

.navtitle{ font-size:12px; letter-spacing:.20em; text-transform:uppercase; color:var(--muted); margin:8px 4px; }
.navlist{ list-style:none; padding:0; margin:0; }
.navlist li{ margin:2px 0; }
.navitem{
  display:flex; align-items:center; gap:8px;
  padding:8px 10px;
  border-radius:10px;
  text-decoration:none;
  color:var(--text);
}
.navitem:hover{ background:rgba(124,58,237,.10); }
.navitem.active{ background:rgba(124,58,237,.18); border:1px solid rgba(124,58,237,.25); }
.navitem code{ font-family:var(--mono); font-size:12px; color:var(--muted); }

.main{
  min-width:0;
  display:flex;
  flex-direction:column;
  gap:14px;
}

.card{
  border:1px solid var(--border);
  border-radius:var(--radius);
  background:linear-gradient(180deg,var(--card),var(--card2));
  box-shadow:var(--shadow);
  overflow:hidden;
}
.card .cardhead{
  padding:14px 16px;
  display:flex; align-items:center; justify-content:space-between;
  gap:12px;
  border-bottom:1px solid var(--border);
}
.card .cardhead h1, .card .cardhead h2{ margin:0; font-size:16px; }
.card .cardbody{ padding:14px 16px; }

h1,h2,h3{margin:0 0 10px}
h1{font-size:20px}
h2{font-size:16px}
p{margin:8px 0; color:var(--muted)}
.meta{ font-size:13px; color:var(--faint); }

.grid2{
  display:grid;
  grid-template-columns: repeat(2, minmax(0,1fr));
  gap:12px;
}
@media (max-width: 980px){
  .app{ grid-template-columns: 1fr; }
  .sidebar{ position:relative; top:auto; height:auto; }
  .grid2{ grid-template-columns: 1fr; }
}

.kpis{display:flex; flex-wrap:wrap; gap:10px}
.kpi{
  border:1px solid var(--border);
  border-radius:12px;
  padding:10px 12px;
  background:rgba(255,255,255,.04);
}
:root:not([data-theme="dark"]) .kpi{ background:rgba(17,24,39,.02); }
.kpi .k{font-size:12px; color:var(--muted); margin-bottom:2px}
.kpi .v{font-size:14px; font-weight:700}

.badge{
  display:inline-flex; align-items:center;
  padding:3px 8px;
  border-radius:999px;
  border:1px solid var(--border);
  font-size:12px;
  color:var(--muted);
  background:rgba(255,255,255,.04);
}
.badge.req{ color:var(--accent2); border-color:rgba(34,197,94,.35); background:rgba(34,197,94,.10); }
.badge.opt{ color:var(--muted); }
.badge.warn{ color:var(--warn); border-color:rgba(245,158,11,.35); background:rgba(245,158,11,.10); }
.badge.bad{ color:var(--bad); border-color:rgba(239,68,68,.35); background:rgba(239,68,68,.10); }
.badge.kind{ color:var(--accent); border-color:rgba(124,58,237,.35); background:rgba(124,58,237,.10); }

.chips{ display:flex; flex-wrap:wrap; gap:6px; }
.chip{
  border:1px solid var(--border);
  background:rgba(255,255,255,.04);
  border-radius:999px;
  padding:3px 8px;
}
.chip code{font-family:var(--mono); font-size:12px; color:var(--muted)}

.callout{
  border:1px solid rgba(124,58,237,.25);
  background:rgba(124,58,237,.10);
  border-radius:12px;
  padding:10px 12px;
  color:var(--muted);
}
.callout strong{color:var(--text)}

pre{
  margin:0;
  padding:12px 12px;
  background:rgba(2,6,23,.65);
  border-top:1px solid rgba(255,255,255,.06);
  overflow:auto;
}
:root:not([data-theme="dark"]) pre{ background:rgba(15,23,42,.06); border-top:1px solid rgba(17,24,39,.06);}
code{ font-family:var(--mono); font-size:13px; }

.tablewrap{ overflow:auto; }
table.keys{
  width:100%;
  border-collapse:separate;
  border-spacing:0;
  min-width:820px;
}
table.keys th, table.keys td{
  border-bottom:1px solid var(--border);
  padding:10px 10px;
  vertical-align:top;
}
table.keys th{
  position:sticky; top:0;
  background:rgba(15,23,42,.88);
  backdrop-filter: blur(8px);
  text-align:left;
  font-size:12px;
  letter-spacing:.08em;
  text-transform:uppercase;
  color:var(--muted);
}
:root:not([data-theme="dark"]) table.keys th{ background:rgba(255,255,255,.92); }
table.keys tr:hover td{ background:rgba(124,58,237,.06); }

.keycell{
  display:flex; align-items:flex-start; justify-content:space-between; gap:10px;
}
.keycell .left{ min-width:0; }
.keycell .left code{word-break:break-word}
.filter{
  width: min(380px, 100%);
  border:1px solid var(--border);
  background:rgba(255,255,255,.04);
  color:var(--text);
  border-radius:12px;
  padding:9px 10px;
  outline:none;
}
:root:not([data-theme="dark"]) .filter{ background:rgba(17,24,39,.03); }
.filter:focus{ border-color:rgba(124,58,237,.40); box-shadow:0 0 0 4px rgba(124,58,237,.12); }

.constraint{
  border:1px solid var(--border);
  border-radius:14px;
  padding:12px 12px;
  background:rgba(255,255,255,.03);
}
:root:not([data-theme="dark"]) .constraint{ background:rgba(17,24,39,.02); }
.constraint .row{
  display:flex; align-items:flex-start; justify-content:space-between; gap:10px; flex-wrap:wrap;
}
.constraint .row .title{ font-weight:700 }
.constraint p{ margin:8px 0 10px; color:var(--muted); }

/* graph */
.graph{
  width:100%;
  min-height:140px;
  overflow:auto;
}
.graph-svg{
  display:block;
}
.graph-node rect{
  fill:rgba(15,23,42,.96);
  stroke:var(--border2);
}
:root:not([data-theme="dark"]) .graph-node rect{
  fill:#ffffff;
}
.graph-node text{
  font-size:12px;
  fill:var(--text);
}
.graph-node:hover rect{
  stroke:var(--accent);
  cursor:pointer;
}
.graph-edge{
  stroke:var(--border2);
  stroke-width:1.1;
  fill:none;
}
.graph-edge-label{
  font-size:10px;
  fill:var(--muted);
  pointer-events:none;
}
.graph-empty{
  font-size:12px;
  color:var(--muted);
}
    )CSS";
}

static std::string siteJs() {
    return R"JS(
(function(){
  const root = document.documentElement;

  function preferredTheme(){
    const stored = localStorage.getItem('csl-theme');
    if(stored === 'dark' || stored === 'light') return stored;
    if(window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches) return 'dark';
    return 'light';
  }

  root.dataset.theme = preferredTheme();

  // Page enter animation (and keep BFCache restores sane)
  const reduceMotion = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  function markReady(){
    if(!document.body) return;
    document.body.classList.remove('preload');
    document.body.classList.add('page-ready');
    document.body.classList.remove('leaving');
  }
  if(document.readyState === 'loading'){
    document.addEventListener('DOMContentLoaded', ()=>requestAnimationFrame(markReady));
  } else {
    requestAnimationFrame(markReady);
  }
  window.addEventListener('pageshow', markReady);

  // Soft page transitions for internal doc links
  document.addEventListener('click', (e)=>{
    if(reduceMotion) return;
    const a = e.target.closest('a');
    if(!a) return;
    if(e.defaultPrevented) return;
    if(a.target && a.target !== '_self') return;
    if(a.hasAttribute('download')) return;
    if(e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;

    const href = a.getAttribute('href');
    if(!href || href.startsWith('#') || href.startsWith('mailto:') || href.startsWith('tel:') || href.startsWith('javascript:')) return;

    let url;
    try { url = new URL(href, window.location.href); } catch(_) { return; }
    if(url.origin !== window.location.origin) return;
    if(url.href === window.location.href) return;

    e.preventDefault();
    document.body.classList.add('leaving');

    const go = ()=>{ window.location.href = url.href; };

    const t = setTimeout(go, 180);
    document.body.addEventListener('transitionend', function onEnd(ev){
      if(ev.propertyName !== 'opacity') return;
      clearTimeout(t);
      document.body.removeEventListener('transitionend', onEnd);
      go();
    });
  }, true);

  // Breadcrumb collapsing ("...") when space is tight + ellipsis menu
  function closeEllMenu(ell){
    const btn = ell.querySelector('[data-crumb-ellipsis-btn]');
    const menu = ell.querySelector('[data-crumb-ellipsis-menu]');
    if(btn) btn.setAttribute('aria-expanded','false');
    if(menu){
      menu.hidden = true;
      menu.style.left = '';
      menu.style.top = '';
    }
  }

  function closeAllEllMenus(){
    document.querySelectorAll('[data-crumb-ellipsis]').forEach(closeEllMenu);
  }

  function rebuildEllMenu(ell, hiddenNodes){
    const menu = ell.querySelector('[data-crumb-ellipsis-menu]');
    const btn = ell.querySelector('[data-crumb-ellipsis-btn]');
    if(!menu || !btn) return;

    menu.innerHTML = '';
    let pathParts = [];
    for(const n of hiddenNodes){
      const a = n.querySelector('a.crumb');
      if(!a) continue;

      const href = a.getAttribute('href');
      if(!href) continue;

      const lbl = (n.getAttribute('data-label') || a.textContent || '').trim();
      if(!lbl) continue;

      pathParts.push(lbl);

      const item = document.createElement('a');
      item.className = 'ellitem';
      item.href = href;
      item.setAttribute('role', 'menuitem');
      item.title = pathParts.join(' > ');
      item.innerHTML = '<span class="muted">&gt;</span><span class="crumbtxt"></span>';
      item.querySelector('.crumbtxt').textContent = lbl;
      menu.appendChild(item);
    }

    btn.disabled = menu.childElementCount === 0;
  }

  function collapseCrumbs(){
    closeAllEllMenus();
    document.querySelectorAll('[data-crumbs]').forEach((nav)=>{
      const nodes = Array.from(nav.querySelectorAll('[data-crumb-node]'));
      const ell = nav.querySelector('[data-crumb-ellipsis]');
      if(!ell || nodes.length < 2) return;

      const nodesOverflow = () => {
        let overflow = false;
        nodes.forEach(n=>{
          const txtElem = n.querySelector('.crumbtxt');
          if(txtElem) {
            overflow = overflow || (txtElem.scrollWidth > txtElem.clientWidth);
          }
        });
        return overflow;
      };

      nodes.forEach(n=>{ n.hidden = false; });
      ell.hidden = true;
      ell.removeAttribute('title');
      rebuildEllMenu(ell, []);

      if(!nodesOverflow()) return;

      const hiddenLabels = [];
      const hiddenNodes = [];
      for(let i=0; i<nodes.length - 1; i++){
        nodes[i].hidden = true;
        hiddenNodes.push(nodes[i]);
        const lbl = nodes[i].getAttribute('data-label') || '';
        if(lbl) hiddenLabels.push(lbl);

        ell.hidden = false;
        if(hiddenLabels.length) ell.title = hiddenLabels.join(' > ');
        rebuildEllMenu(ell, hiddenNodes);
        if(!nodesOverflow()) return;
      }
    });
  }
  if(document.readyState === 'loading'){
    document.addEventListener('DOMContentLoaded', collapseCrumbs);
  } else {
    collapseCrumbs();
  }
  window.addEventListener('resize', collapseCrumbs);
  window.addEventListener('scroll', closeAllEllMenus, true);

  // Toggle ellipsis menu
  document.addEventListener('click', (e)=>{
    const btn = e.target.closest('[data-crumb-ellipsis-btn]');
    if(btn){
      e.preventDefault();
      e.stopPropagation();

      const ell = btn.closest('[data-crumb-ellipsis]');
      const menu = ell && ell.querySelector('[data-crumb-ellipsis-menu]');
      if(!ell || !menu) return;

      const wasOpen = !menu.hidden;
      closeAllEllMenus();
      if(wasOpen) return;
      if(menu.childElementCount === 0) return;

      menu.hidden = false;
      btn.setAttribute('aria-expanded','true');

      const r = btn.getBoundingClientRect();
      const pad = 8;
      const maxLeft = window.innerWidth - menu.offsetWidth - pad;
      const left = Math.max(pad, Math.min(r.left, maxLeft));
      const maxTop = window.innerHeight - menu.offsetHeight - pad;
      const top = Math.max(pad, Math.min(r.bottom + pad, maxTop));
      menu.style.left = left + 'px';
      menu.style.top = top + 'px';

      const first = menu.querySelector('a.ellitem');
      if(first) first.focus({preventScroll:true});
      return;
    }

    if(!e.target.closest('[data-crumb-ellipsis]')){
      closeAllEllMenus();
    }
  });

  document.addEventListener('keydown', (e)=>{
    if(e.key === 'Escape') closeAllEllMenus();
  });

  const toggle = document.querySelector('[data-theme-toggle]');
  if(toggle){
    toggle.setAttribute('aria-pressed', root.dataset.theme === 'dark' ? 'true' : 'false');
    toggle.addEventListener('click', ()=>{
      const next = root.dataset.theme === 'dark' ? 'light' : 'dark';
      root.dataset.theme = next;
      localStorage.setItem('csl-theme', next);
      toggle.setAttribute('aria-pressed', next === 'dark' ? 'true' : 'false');
    });
  }

  function copyText(text){
    if(navigator.clipboard && navigator.clipboard.writeText){
      navigator.clipboard.writeText(text).catch(()=>fallback(text));
    } else {
      fallback(text);
    }
  }

  function fallback(text){
    const ta = document.createElement('textarea');
    ta.value = text;
    ta.style.position = 'fixed';
    ta.style.opacity = '0';
    ta.style.pointerEvents = 'none';
    document.body.appendChild(ta);
    ta.select();
    try { document.execCommand('copy'); } catch(_) {}
    document.body.removeChild(ta);
  }

  document.addEventListener('click', (e)=>{
    const btn = e.target.closest('[data-copy],[data-copy-el]');
    if(!btn) return;

    e.preventDefault();

    let value = btn.getAttribute('data-copy');
    const elId = btn.getAttribute('data-copy-el');
    if(elId){
      const el = document.getElementById(elId);
      if(el) value = el.textContent || '';
    }
    if(typeof value !== 'string') value = '';

    copyText(value);

    btn.classList.add('copied');
    setTimeout(()=>btn.classList.remove('copied'), 900);
  });

  document.querySelectorAll('input[data-filter-table]').forEach((input)=>{
    const tableId = input.getAttribute('data-filter-table');
    const table = document.getElementById(tableId);
    if(!table) return;
    const rows = Array.from(table.querySelectorAll('tbody tr'));

    function apply(){
      const q = (input.value || '').trim().toLowerCase();
      for(const r of rows){
        const hay = (r.getAttribute('data-search') || '').toLowerCase();
        r.style.display = (!q || hay.includes(q)) ? '' : 'none';
      }
    }

    input.addEventListener('input', apply);
    apply();
  });

  function renderStructureGraph(container){
    const raw = container.getAttribute('data-structure-graph');
    if(!raw) return;

    let data;
    try {
      data = JSON.parse(raw);
    } catch(_) {
      return;
    }
    if(!data || !Array.isArray(data.nodes) || !data.nodes.length){
      container.innerHTML = '<p class="graph-empty">No nested tables to visualize.</p>';
      return;
    }

    const nodes = data.nodes;
    const edges = Array.isArray(data.edges) ? data.edges : [];

    const svgNS = 'http://www.w3.org/2000/svg';

    // Measure text so nodes can size to their labels.
    const canvas = document.createElement('canvas');
    const ctx = canvas.getContext('2d');
    const fontFamily = (getComputedStyle(document.body).fontFamily || 'system-ui, sans-serif');
    ctx.font = '12px ' + fontFamily;
    const textWidth = (s)=> ctx.measureText(s == null ? '' : String(s)).width;

    const MIN_W = 80;
    const MAX_W = 240;
    const PAD_X = 14;
    const NODE_H = 32;
    const GAP = 24;
    const MARGIN_X = 30;
    const topMargin = 30;
    const levelGap = 120;

    function ellipsize(s, maxPx){
      s = (s == null) ? '' : String(s);
      if(textWidth(s) <= maxPx) return s;
      const ell = '…';
      let lo = 0, hi = s.length;
      while(lo < hi){
        const mid = ((lo + hi) / 2) | 0;
        const candidate = s.slice(0, mid) + ell;
        if(textWidth(candidate) <= maxPx) lo = mid + 1;
        else hi = mid;
      }
      const cut = Math.max(0, lo - 1);
      return s.slice(0, cut) + ell;
    }

    // Group nodes by depth
    const levels = {};
    let maxDepth = 0;
    nodes.forEach((n)=>{
      const d = (typeof n.depth === 'number' && n.depth >= 0) ? n.depth : 0;
      if(!levels[d]) levels[d] = [];
      levels[d].push(n);
      if(d > maxDepth) maxDepth = d;
    });

    // Precompute per-node sizing + display label
    const geom = {};
    const maxTextW = MAX_W - PAD_X*2;
    nodes.forEach((n)=>{
      const shown = ellipsize(n.label, maxTextW);
      const w = Math.max(MIN_W, Math.min(MAX_W, Math.ceil(textWidth(shown) + PAD_X*2)));
      geom[n.id] = { w, h: NODE_H, shown, full: n.label || '' };
    });

    // Compute required width so nodes don't overlap; render at 1:1 and let the container scroll.
    let width = 900;
    for(let depth = 0; depth <= maxDepth; depth++){
      const row = levels[depth] || [];
      if(!row.length) continue;
      const rowW =
        row.reduce((acc, n)=> acc + ((geom[n.id] && geom[n.id].w) ? geom[n.id].w : MIN_W), 0) +
        GAP * (row.length - 1);
      width = Math.max(width, rowW + MARGIN_X*2);
    }
    const height = topMargin + (maxDepth + 1) * levelGap;

    const svg = document.createElementNS(svgNS,'svg');
    svg.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
    svg.setAttribute('width', width);
    svg.setAttribute('height', height);
    svg.classList.add('graph-svg');

    const pos = {};
    for(let depth = 0; depth <= maxDepth; depth++){
      const row = levels[depth] || [];
      if(!row.length) continue;

      const rowW =
        row.reduce((acc, n)=> acc + ((geom[n.id] && geom[n.id].w) ? geom[n.id].w : MIN_W), 0) +
        GAP * (row.length - 1);
      let x = (width - rowW) / 2;

      row.forEach((n)=>{
        const g = geom[n.id] || { w: MIN_W, h: NODE_H, shown: n.label || '', full: n.label || '' };
        pos[n.id] = {
          x: x + g.w/2,
          y: topMargin + depth * levelGap,
          w: g.w,
          h: g.h,
          shown: g.shown,
          full: g.full
        };
        x += g.w + GAP;
      });
    }

    // Edges first so they sit under the nodes
    edges.forEach((e)=>{
      const from = pos[e.from];
      const to = pos[e.to];
      if(!from || !to) return;

      const startX = from.x;
      const startY = from.y + from.h/2;
      const endX = to.x;
      const endY = to.y - to.h/2;
      const midY = (startY + endY) / 2;

      const path = document.createElementNS(svgNS, 'path');
      const d = ['M', startX, startY, 'C', startX, midY, endX, midY, endX, endY].join(' ');
      path.setAttribute('d', d);
      path.setAttribute('class', 'graph-edge');
      svg.appendChild(path);
    });

    nodes.forEach((n)=>{
      const p = pos[n.id];
      if(!p) return;

      const g = document.createElementNS(svgNS, 'g');
      g.setAttribute('transform', 'translate(' + (p.x - p.w/2) + ',' + (p.y - p.h/2) + ')');
      g.classList.add('graph-node');
      if(n.file) g.dataset.file = n.file;

      // Tooltip: show full path derived from the id.
      const title = document.createElementNS(svgNS, 'title');
      const parts = (n.id || '').split('\u001f').filter(Boolean);
      title.textContent =
        (data.schema ? (data.schema + (parts.length ? ' > ' : '')) : '') +
        (parts.join(' > ') || (data.schema || ''));
      g.appendChild(title);

      const rect = document.createElementNS(svgNS, 'rect');
      rect.setAttribute('width', p.w);
      rect.setAttribute('height', p.h);
      rect.setAttribute('rx', 10);
      rect.setAttribute('ry', 10);

      const text = document.createElementNS(svgNS, 'text');
      text.setAttribute('x', p.w/2);
      text.setAttribute('y', p.h/2 + 4);
      text.setAttribute('text-anchor', 'middle');
      text.textContent = p.shown;

      g.appendChild(rect);
      g.appendChild(text);
      svg.appendChild(g);
    });

    container.innerHTML = '';
    container.appendChild(svg);
  }

  function initStructureGraphs(){
    const containers = document.querySelectorAll('[data-structure-graph]');
    if(!containers.length) return;

    containers.forEach((el)=>{
      renderStructureGraph(el);
      el.addEventListener('click', (e)=>{
        const g = e.target.closest('.graph-node');
        if(!g) return;
        const file = g.dataset.file;
        if(file){
          window.location.href = file;
        }
      });
    });
  }

  if(document.readyState === 'loading'){
    document.addEventListener('DOMContentLoaded', initStructureGraphs);
  } else {
    initStructureGraphs();
  }
})();
    )JS";
}

// -------------------- Path formatting + slugs --------------------

static std::string slugify(const std::string& s) {
    if (s == "*") return "wildcard";
    if (s == "*[]") return "wildcard-array";
    if (s == "[]") return "array";
    std::ostringstream o;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') o << char(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o << c;
        else if (c == '_' || c == '-') o << '-';
        else if (c == '`') continue;
        else o << '-';
    }
    std::string r = o.str();
    while (!r.empty() && r.back() == '-') r.pop_back();
    while (!r.empty() && r.front() == '-') r.erase(r.begin());
    return r.empty() ? std::string("page") : r;
}

static std::string joinWithDot(const std::vector<std::string>& segs) {
    std::ostringstream os;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (i) os << '.';
        os << segs[i];
    }
    return os.str();
}

static std::string toLowerAlphaNum(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) o.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return o;
}

static std::string toLowerCamelAlphaNum(const std::string& s) {
    // "bin-dependencies" -> "binDependencies"
    std::string out;
    bool upperNext = false;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (out.empty()) {
                out.push_back(lower);
            } else if (upperNext) {
                out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(lower))));
                upperNext = false;
            } else {
                out.push_back(lower);
            }
        } else {
            upperNext = !out.empty();
        }
    }
    if (out.size() > 1 && out.back() == 's') {
        // tiny heuristic: members -> member
        out.pop_back();
    }
    return out.empty() ? std::string("key") : out;
}

static std::string dynamicKeyPlaceholder(const std::vector<std::string>& parentPath) {
    if (parentPath.empty()) return "<key>";
    std::string base = parentPath.back();
    if (base.size() >= 2 && base.substr(base.size() - 2) == "[]") base = base.substr(0, base.size() - 2);
    if (base == "*" || base.empty()) return "<key>";
    return "<" + toLowerCamelAlphaNum(base) + "Key>";
}

static std::string displaySegment(const std::vector<std::string>& prefix, const std::string& seg) {
    if (seg == "*") return dynamicKeyPlaceholder(prefix);
    if (seg == "*[]") return dynamicKeyPlaceholder(prefix) + "[]";
    return seg;
}

static std::string displayPath(const std::vector<std::string>& path) {
    std::vector<std::string> segs;
    segs.reserve(path.size());
    std::vector<std::string> prefix;
    prefix.reserve(path.size());
    for (const auto& seg : path) {
        segs.push_back(displaySegment(prefix, seg));
        prefix.push_back(seg);
    }
    return joinWithDot(segs);
}

static std::string pathKey(const std::vector<std::string>& path) {
    // delimiter unlikely to appear in keys (and not used in filenames)
    std::ostringstream os;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i) os << '\x1f';
        os << path[i];
    }
    return os.str();
}

static std::string pageFileFor(const std::string& schemaName, const std::vector<std::string>& tablePath) {
    std::ostringstream f;
    f << slugify(schemaName);
    for (const auto& seg : tablePath) f << '-' << slugify(seg);
    f << ".html";
    return f.str();
}

static std::string schemaFileFor(const std::string& schemaName) {
    return slugify(schemaName) + ".html";
}

// -------------------- Html generator --------------------

struct HtmlPagesGen {
    struct TablePageMeta {
        std::vector<std::string> path;
        std::shared_ptr<TableType> table;
        std::string filename;
    };

    std::unordered_map<std::string, std::string> pages;

    // schemaName -> list of planned table pages (excluding schema root page)
    std::unordered_map<std::string, std::vector<TablePageMeta>> planned;
    // schemaName -> pathKey -> filename
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> pathToFile;

    std::string renderSchemaGraphCard(
        const std::string& schemaName,
        const std::shared_ptr<TableType>& rootTable
    ) const {
        std::vector<GraphNode> nodes;
        std::vector<GraphEdge> edges;

        // Root node: the schema itself
        GraphNode rootNode;
        rootNode.id = pathKey({}); // empty path
        rootNode.label = schemaName;
        rootNode.file = schemaFileFor(schemaName);
        rootNode.depth = 0;
        nodes.push_back(rootNode);

        auto itPlanned = planned.find(schemaName);
        if (itPlanned != planned.end()) {
            const auto& metas = itPlanned->second;
            // Nodes for each nested table
            for (const auto& meta : metas) {
                GraphNode n;
                n.id = pathKey(meta.path);
                if (meta.path.empty()) {
                    n.label = schemaName;
                } else {
                    std::vector<std::string> parentPath = meta.path;
                    const std::string seg = parentPath.back();
                    parentPath.pop_back();
                    n.label = displaySegment(parentPath, seg);
                }
                n.depth = meta.path.size();

                n.file.clear();
                auto itSchema = pathToFile.find(schemaName);
                if (itSchema != pathToFile.end()) {
                    auto itFile = itSchema->second.find(n.id);
                    if (itFile != itSchema->second.end()) {
                        n.file = itFile->second;
                    }
                }
                nodes.push_back(std::move(n));
            }

            // Edges based on parent/child paths
            for (const auto& meta : metas) {
                if (meta.path.empty()) continue;
                std::vector<std::string> parentPath = meta.path;
                const std::string seg = parentPath.back();
                parentPath.pop_back();

                GraphEdge e;
                e.from = pathKey(parentPath);     // parent id (root = "")
                e.to = pathKey(meta.path);        // child id
                e.label = displaySegment(parentPath, seg);
                edges.push_back(std::move(e));
            }
        }

        std::ostringstream out;
        out << "<div class=\"card\">";
        out << "<div class=\"cardhead\"><h2>Structure graph</h2></div>";
        out << "<div class=\"cardbody\">";

        if (nodes.size() <= 1) {
            out << "<p class=\"meta\">This schema has no nested tables to visualize.</p>";
        } else {
            std::string json = buildStructureGraphJson(schemaName, nodes, edges);
            out << "<div class=\"graph\" data-structure-graph=\""
                << htmlEscape(json)
                << "\">";
            out << "<noscript><p class=\"meta\">Enable JavaScript to see the structure graph.</p></noscript>";
            out << "</div>";
        }

        out << "</div></div>";
        return out.str();
    }

    std::string renderTableGraphCard(
        const std::string& schemaName,
        const TablePageMeta& meta
    ) const {
        std::vector<GraphNode> nodes;
        std::vector<GraphEdge> edges;

        const std::string centerId = pathKey(meta.path);

        // Center node = this table
        GraphNode center;
        center.id = centerId;
        if (meta.path.empty()) {
            center.label = schemaName;
        } else {
            std::vector<std::string> parentPath = meta.path;
            const std::string seg = parentPath.back();
            parentPath.pop_back();
            center.label = displaySegment(parentPath, seg);
        }
        center.file = meta.filename;
        center.depth = 0;
        nodes.push_back(center);

        auto itSchema = pathToFile.find(schemaName);

        auto addChild = [&](const std::string& seg, const std::shared_ptr<TableType>& child) {
            (void)child; // child type is not needed for now, we only need the path
            std::vector<std::string> childPath = meta.path;
            childPath.push_back(seg);

            GraphNode n;
            n.id = pathKey(childPath);
            n.label = displaySegment(meta.path, seg);
            n.depth = 1;

            n.file.clear();
            if (itSchema != pathToFile.end()) {
                auto itFile = itSchema->second.find(n.id);
                if (itFile != itSchema->second.end()) {
                    n.file = itFile->second;
                }
            }
            nodes.push_back(n);

            GraphEdge e;
            e.from = centerId;
            e.to = n.id;
            e.label = displaySegment(meta.path, seg);
            edges.push_back(std::move(e));
        };

        // Immediate children: explicit keys
        for (const auto& kd : meta.table->getExplicitKeys()) {
            auto t = kd->getType();
            if (!t) continue;

            if (t->getKind() == CSLType::Kind::Table) {
                auto child = std::static_pointer_cast<TableType>(t);
                addChild(kd->getName(), child);
                continue;
            }
            if (t->getKind() == CSLType::Kind::Array) {
                auto at = std::static_pointer_cast<ArrayType>(t);
                if (at->getElementType() &&
                    at->getElementType()->getKind() == CSLType::Kind::Table) {
                    auto child = std::static_pointer_cast<TableType>(at->getElementType());
                    addChild(kd->getName() + "[]", child);
                    continue;
                }
            }
        }

        // Wildcard child
        if (meta.table->getWildcardKey()) {
            const auto& wk = meta.table->getWildcardKey();
            auto t = wk->getType();
            if (t && t->getKind() == CSLType::Kind::Table) {
                auto child = std::static_pointer_cast<TableType>(t);
                addChild("*", child);
            } else if (t && t->getKind() == CSLType::Kind::Array) {
                auto at = std::static_pointer_cast<ArrayType>(t);
                if (at->getElementType() &&
                    at->getElementType()->getKind() == CSLType::Kind::Table) {
                    auto child = std::static_pointer_cast<TableType>(at->getElementType());
                    addChild("*[]", child);
                }
            }
        }

        std::ostringstream out;
        out << "<div class=\"card\">";
        out << "<div class=\"cardhead\"><h2>Structure graph</h2></div>";
        out << "<div class=\"cardbody\">";

        if (nodes.size() <= 1) {
            out << "<p class=\"meta\">This table has no nested tables.</p>";
        } else {
            std::string json = buildStructureGraphJson(schemaName, nodes, edges);
            out << "<div class=\"graph\" data-structure-graph=\""
                << htmlEscape(json)
                << "\">";
            out << "<noscript><p class=\"meta\">Enable JavaScript to see the structure graph.</p></noscript>";
            out << "</div>";
        }

        out << "</div></div>";
        return out.str();
    }

    void addPage(const std::string& name, const std::string& content) {
        pages[name] = content;
    }

    std::string renderTopbar(const std::string& schemaName, const std::string& subtitleHtml, const std::vector<std::string>* tablePath = nullptr) {
        std::ostringstream out;
        out << "<header class=\"topbar\">";
        out << "<div class=\"brand\"><span class=\"dot\"></span>";

        out << "<nav class=\"crumbs\" data-crumbs>";
        out << "<a class=\"crumb\" href=\"index.html\" style=\"text-decoration:none\"><span class=\"crumbtxt\">CSL Docs</span></a>";

        if (!schemaName.empty()) {
            const std::string schemaFile = schemaFileFor(schemaName);
            out << "<span class=\"sep\">/</span>";
            out << "<a class=\"crumb\" href=\"" << htmlEscape(schemaFile) << "\" style=\"text-decoration:none\"><span class=\"crumbtxt\">"
                << htmlEscape(schemaName) << "</span></a>";

            if (tablePath && !tablePath->empty()) {
                // Insert an ellipsis placeholder after the schema; JS will toggle it on when needed.
                out << "<span class=\"node ellipsis\" data-crumb-ellipsis hidden>"
                    << "<span class=\"sep\">&gt;</span>"
                    << "<button class=\"crumb\" type=\"button\" data-crumb-ellipsis-btn aria-haspopup=\"menu\" aria-expanded=\"false\">"
                    << "<span class=\"crumbtxt\">...</span></button>"
                    << "<div class=\"ellmenu\" data-crumb-ellipsis-menu role=\"menu\" aria-label=\"Hidden breadcrumbs\" hidden></div>"
                    << "</span>";

                std::vector<std::string> prefix;
                prefix.reserve(tablePath->size());
                for (size_t i = 0; i < tablePath->size(); ++i) {
                    const auto& seg = (*tablePath)[i];
                    const std::string label = displaySegment(prefix, seg);

                    prefix.push_back(seg);
                    const bool isLast = (i + 1 == tablePath->size());
                    out << "<span class=\"node\" data-crumb-node=\"1\" data-label=\"" << htmlEscape(label) << "\">"
                        << "<span class=\"sep\">&gt;</span>";

                    if (isLast) {
                        out << "<span class=\"crumb current\" aria-current=\"page\"><span class=\"crumbtxt\">"
                            << htmlEscape(label) << "</span></span>";
                    } else {
                        const std::string href = pageFileFor(schemaName, prefix);
                        out << "<a class=\"crumb\" href=\"" << htmlEscape(href) << "\" style=\"text-decoration:none\">"
                            << "<span class=\"crumbtxt\">" << htmlEscape(label) << "</span></a>";
                    }
                    out << "</span>";
                }
            }
        }

        out << "</nav>";
        out << "</div>";

        out << "<div class=\"actions\">";
        if (!subtitleHtml.empty()) out << subtitleHtml;
        out << "<button class=\"iconbtn\" type=\"button\" data-theme-toggle aria-label=\"Toggle dark mode\">";
        out << "<span>Theme</span><span class=\"kbd\">⌘</span>";
        out << "</button>";
        out << "</div>";
        out << "</header>";
        return out.str();
    }

    std::string renderSidebar(const std::string& schemaName, const std::string& currentFile) {
        std::ostringstream out;
        out << "<aside class=\"sidebar\">";
        out << "<div class=\"navtitle\">Navigation</div>";
        out << "<ul class=\"navlist\">";

        out << "<li><a class=\"navitem " << (currentFile == "index.html" ? "active" : "") << "\" href=\"index.html\">Index</a></li>";

        if (!schemaName.empty()) {
            const std::string schemaFile = schemaFileFor(schemaName);
            out << "<li><a class=\"navitem " << (currentFile == schemaFile ? "active" : "") << "\" href=\"" << htmlEscape(schemaFile) << "\">";
            out << "Schema <code>" << htmlEscape(schemaName) << "</code></a></li>";

            auto it = planned.find(schemaName);
            if (it != planned.end() && !it->second.empty()) {
                out << "<div class=\"navtitle\">Tables</div>";
                // sort by display path for stable nav
                std::vector<TablePageMeta> items = it->second;
                std::sort(items.begin(), items.end(), [](const TablePageMeta& a, const TablePageMeta& b) {
                    return displayPath(a.path) < displayPath(b.path);
                });

                for (const auto& p : items) {
                    std::string label = displayPath(p.path);
                    out << "<li><a class=\"navitem " << (currentFile == p.filename ? "active" : "") << "\" href=\"" << htmlEscape(p.filename) << "\">";
                    out << "<code>" << htmlEscape(label) << "</code></a></li>";
                }
            }
        }

        out << "</ul>";
        out << "</aside>";
        return out.str();
    }

    std::string pageWrap(const std::string& title, const std::string& schemaName, const std::string& currentFile, const std::string& mainHtml, const std::string& subtitleHtml = "", const std::vector<std::string>* tablePath = nullptr) {
        std::ostringstream out;
        out << "<!DOCTYPE html><html><head>";
        out << "<meta charset=\"utf-8\">";
        out << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
        out << "<title>" << htmlEscape(title) << "</title>";
        out << "<link rel=\"stylesheet\" href=\"site.css\">";
        out << "<script defer src=\"site.js\"></script>";
        out << "</head><body class=\"preload\">";
        out << "<noscript><style>body.preload{opacity:1 !important; transform:none !important;}</style></noscript>";
        out << renderTopbar(schemaName, subtitleHtml, tablePath);
        out << "<div class=\"app\">";
        out << renderSidebar(schemaName, currentFile);
        out << "<main class=\"main\">" << mainHtml << "</main>";
        out << "</div></body></html>";
        return out.str();
    }

    static bool isIdentifierExpr(const std::shared_ptr<Expr>& e, std::string& nameOut) {
        if (!e || e->getKind() != Expr::Kind::Identifier) return false;
        nameOut = std::static_pointer_cast<IdentifierExpr>(e)->getName();
        return true;
    }

    static std::string keyAnchorId(const std::string& keyName) {
        return "k-" + slugify(keyName);
    }

    // -------------------- Planning: recursively create table pages --------------------

    void planTablesForSchema(const std::string& schemaName, const std::shared_ptr<TableType>& rootTable) {
        planned[schemaName].clear();
        pathToFile[schemaName].clear();

        // Avoid duplicates (in case of repeated structures)
        std::unordered_set<std::string> seen;

        std::function<void(const std::shared_ptr<TableType>&, const std::vector<std::string>&)> walk =
            [&](const std::shared_ptr<TableType>& table, const std::vector<std::string>& path) {
                // create page for this table path (but do not create for schema root)
                if (!path.empty()) {
                    std::string pk = pathKey(path);
                    if (!seen.count(pk)) {
                        seen.insert(pk);
                        TablePageMeta meta;
                        meta.path = path;
                        meta.table = table;
                        meta.filename = pageFileFor(schemaName, path);
                        planned[schemaName].push_back(meta);
                        pathToFile[schemaName][pk] = meta.filename;
                    }
                }

                // children: explicit keys
                for (const auto& kd : table->getExplicitKeys()) {
                    auto t = kd->getType();
                    if (!t) continue;

                    if (t->getKind() == CSLType::Kind::Table) {
                        auto child = std::static_pointer_cast<TableType>(t);
                        auto childPath = path;
                        childPath.push_back(kd->getName());
                        walk(child, childPath);
                        continue;
                    }
                    if (t->getKind() == CSLType::Kind::Array) {
                        auto at = std::static_pointer_cast<ArrayType>(t);
                        if (at->getElementType() && at->getElementType()->getKind() == CSLType::Kind::Table) {
                            auto child = std::static_pointer_cast<TableType>(at->getElementType());
                            auto childPath = path;
                            childPath.push_back(kd->getName() + "[]");
                            walk(child, childPath);
                            continue;
                        }
                    }
                }

                // wildcard value type
                if (table->getWildcardKey()) {
                    const auto& wk = table->getWildcardKey();
                    auto t = wk->getType();
                    if (t && t->getKind() == CSLType::Kind::Table) {
                        auto child = std::static_pointer_cast<TableType>(t);
                        auto childPath = path;
                        childPath.push_back("*");
                        walk(child, childPath);
                    } else if (t && t->getKind() == CSLType::Kind::Array) {
                        auto at = std::static_pointer_cast<ArrayType>(t);
                        if (at->getElementType() && at->getElementType()->getKind() == CSLType::Kind::Table) {
                            auto child = std::static_pointer_cast<TableType>(at->getElementType());
                            auto childPath = path;
                            childPath.push_back("*[]");
                            walk(child, childPath);
                        }
                    }
                }
            };

        walk(rootTable, {});
    }

    // -------------------- Rendering helpers --------------------

    std::string linkToPageIfExists(const std::string& schemaName, const std::vector<std::string>& path, const std::string& labelHtml) {
        auto it = pathToFile.find(schemaName);
        if (it == pathToFile.end()) return labelHtml;
        auto it2 = it->second.find(pathKey(path));
        if (it2 == it->second.end()) return labelHtml;
        std::ostringstream out;
        out << "<a class=\"link\" href=\"" << htmlEscape(it2->second) << "\">" << labelHtml << "</a>";
        return out.str();
    }

    std::string typeBadgesHtml(const std::shared_ptr<CSLType>& type) {
        std::ostringstream b;
        if (!type) return "";
        if (type->getKind() == CSLType::Kind::AnyTable || type->getKind() == CSLType::Kind::AnyArray) {
            b << "<span class=\"badge warn\">Unvalidated</span>";
        } else if (type->getKind() == CSLType::Kind::Union) {
            b << "<span class=\"badge kind\">Union</span>";
        } else if (isEnumPrimitive(type)) {
            b << "<span class=\"badge kind\">Enum</span>";
        }
        return b.str();
    }

    std::string requiredBadge(bool optional) {
        if (optional) return "<span class=\"badge opt\">Optional</span>";
        return "<span class=\"badge req\">Required</span>";
    }

    void renderKeysTable(const std::string& schemaName, const std::vector<std::string>& tablePath, const std::shared_ptr<TableType>& table, std::ostringstream& out) {
        const std::string tableId = "keys-table";
        out << "<div class=\"tablewrap\">";
        out << "<table id=\"" << tableId << "\" class=\"keys\">";
        out << "<thead><tr>";
        out << "<th style=\"min-width:240px\">Key</th>";
        out << "<th style=\"min-width:260px\">Type</th>";
        out << "<th>Required</th>";
        out << "<th style=\"min-width:160px\">Default</th>";
        out << "<th style=\"min-width:220px\">Annotations</th>";
        out << "<th style=\"min-width:110px\">Details</th>";
        out << "</tr></thead><tbody>";

        // explicit keys sorted
        std::vector<std::string> keyOrder;
        keyOrder.reserve(table->getExplicitKeys().size());
        for (const auto& kd : table->getExplicitKeys()) keyOrder.push_back(kd->getName());
        std::sort(keyOrder.begin(), keyOrder.end());

        std::unordered_map<std::string, std::shared_ptr<TableType::KeyDefinition>> map;
        for (const auto& kd : table->getExplicitKeys()) map[kd->getName()] = kd;

        auto renderRow = [&](const std::string& keyName,
                             const std::string& keyDisplay,
                             bool isOptional,
                             const std::shared_ptr<CSLType>& type,
                             const std::optional<std::pair<std::string, std::unique_ptr<Type::Type>>>& defaultValue,
                             const std::vector<std::shared_ptr<Annotation>>& annotations,
                             const std::vector<std::string>& childTablePath,
                             bool isDynamicKey) {

            const std::string typeStr = type ? typeLabel(type) : "";
            const std::string defStr = defaultValue.has_value() ? defaultValue.value().first : "";
            const std::string annPlain = renderAnnotationsPlain(annotations);

            std::ostringstream search;
            search << keyDisplay << " " << typeStr << " " << (isOptional ? "optional" : "required") << " " << defStr << " " << annPlain;

            std::string anchor = keyAnchorId(keyName);
            out << "<tr id=\"" << htmlEscape(anchor) << "\" data-search=\"" << htmlEscape(search.str()) << "\">";

            // Key + copy path
            out << "<td>";
            out << "<div class=\"keycell\">";
            out << "<div class=\"left\">";
            out << "<code>" << htmlEscape(keyDisplay) << "</code>";
            if (isDynamicKey) out << "<div style=\"margin-top:6px\"><span class=\"badge kind\">Dynamic key</span></div>";
            out << "</div>";

            // Copy path
            std::vector<std::string> fullPath = tablePath;
            fullPath.push_back(keyName);
            std::string copyPathDisplay = displayPath(fullPath);
            out << "<button class=\"iconbtn\" type=\"button\" data-copy=\"" << htmlEscape(copyPathDisplay) << "\" aria-label=\"Copy path\">";
            out << "Copy";
            out << "</button>";

            out << "</div>";
            out << "</td>";

            // Type cell
            out << "<td>";
            out << "<div class=\"chips\" style=\"margin-bottom:6px\">";
            out << "<span class=\"chip\"><code>" << htmlEscape(typeStr) << "</code></span>";
            std::string badges = typeBadgesHtml(type);
            if (!badges.empty()) out << badges;
            if (type && type->getKind() == CSLType::Kind::Table) {
                out << "<span class=\"chip\"><code>" << countKeys(std::static_pointer_cast<TableType>(type)) << " keys</code></span>";
                out << "<span class=\"chip\"><code>depth " << nestedDepth(type) << "</code></span>";
            }
            if (type && type->getKind() == CSLType::Kind::Array) {
                out << "<span class=\"chip\"><code>depth " << nestedDepth(type) << "</code></span>";
            }
            out << "</div>";
            out << "</td>";

            // Required
            out << "<td>" << requiredBadge(isOptional) << "</td>";

            // Default
            out << "<td>";
            if (!defStr.empty()) out << "<code>" << htmlEscape(defStr) << "</code>";
            out << "</td>";

            // Annotations
            out << "<td>";
            out << renderAnnotationsHtml(annotations);
            out << "</td>";

            // Details
            out << "<td>";
            if (!childTablePath.empty()) {
                // Always link to a dedicated table page if we planned one
                std::string label = "<span class=\"badge kind\">Open</span>";
                out << linkToPageIfExists(schemaName, childTablePath, label);
            } else {
                out << "<span class=\"meta\">—</span>";
            }
            out << "</td>";

            out << "</tr>";
        };

        for (const auto& keyName : keyOrder) {
            const auto& kd = map[keyName];
            std::string keyDisplay = quoteIdentifier(kd->getName());

            std::vector<std::string> childPath;
            if (kd->getType()) {
                if (kd->getType()->getKind() == CSLType::Kind::Table) {
                    childPath = tablePath;
                    childPath.push_back(kd->getName());
                } else if (kd->getType()->getKind() == CSLType::Kind::Array) {
                    auto at = std::static_pointer_cast<ArrayType>(kd->getType());
                    if (at->getElementType() && at->getElementType()->getKind() == CSLType::Kind::Table) {
                        childPath = tablePath;
                        childPath.push_back(kd->getName() + "[]");
                    }
                }
            }

            renderRow(
                kd->getName(),
                keyDisplay,
                kd->getIsOptional(),
                kd->getType(),
                kd->getDefaultValue(),
                kd->getAnnotations(),
                childPath,
                false
            );
        }

        // wildcard row: show as <somethingName> rather than "*"
        if (table->getWildcardKey()) {
            const auto& wk = table->getWildcardKey();
            std::string dyn = dynamicKeyPlaceholder(tablePath);
            std::string display = dyn;

            std::vector<std::string> childPath;
            if (wk->getType()) {
                if (wk->getType()->getKind() == CSLType::Kind::Table) {
                    childPath = tablePath;
                    childPath.push_back("*");
                } else if (wk->getType()->getKind() == CSLType::Kind::Array) {
                    auto at = std::static_pointer_cast<ArrayType>(wk->getType());
                    if (at->getElementType() && at->getElementType()->getKind() == CSLType::Kind::Table) {
                        childPath = tablePath;
                        childPath.push_back("*[]");
                        display = dyn + "[]";
                    }
                }
            }

            // use "*" as internal key name for anchors + copy path
            renderRow(
                "*",
                display,
                wk->getIsOptional(),
                wk->getType(),
                wk->getDefaultValue(),
                wk->getAnnotations(),
                childPath,
                true
            );
        }

        out << "</tbody></table></div>";
        // search box (wired by site.js via data-filter-table)
        // NOTE: The input itself is placed by caller in the card header so layout stays clean.
    }

    void renderConstraints(const std::shared_ptr<TableType>& table, const std::unordered_set<std::string>& knownKeys, std::ostringstream& out) {
        const auto& constraints = table->getConstraints();
        if (constraints.empty()) return;

        auto linkKey = [&](const std::string& key) -> std::string {
            if (knownKeys.count(key)) {
                std::ostringstream a;
                a << "<a class=\"link\" href=\"#" << htmlEscape(keyAnchorId(key)) << "\"><code>" << htmlEscape(key) << "</code></a>";
                return a.str();
            }
            std::ostringstream c;
            c << "<code>" << htmlEscape(key) << "</code>";
            return c.str();
        };

        out << "<div class=\"card\">";
        out << "<div class=\"cardhead\"><h2>Constraints</h2></div>";
        out << "<div class=\"cardbody\">";
        out << "<p class=\"meta\">Rules declared in this table’s <code>constraints</code> block.</p>";

        for (const auto& c : constraints) {
            std::string kindBadge;
            std::string title;
            std::string sentence;
            std::string codeLine;

            if (c->getKind() == Constraint::Kind::Conflict) {
                auto cc = std::static_pointer_cast<ConflictConstraint>(c);
                kindBadge = "<span class=\"badge bad\">Conflict</span>";
                title = "Mutual exclusion";
                std::string a, b;
                if (isIdentifierExpr(cc->getFirstExpr(), a)) {
                    if (isIdentifierExpr(cc->getSecondExpr(), b)) {
                        sentence = "Keys " + linkKey(a) + " and " + linkKey(b) + " cannot both be present.";
                    } else {
                        sentence = "Key " + linkKey(a) + " cannot be present when condition <code>" + htmlEscape(exprToString(cc->getSecondExpr())) + "</code> holds.";
                    }
                } else {
                    if (isIdentifierExpr(cc->getSecondExpr(), b)) {
                        sentence = "Condition <code>" + htmlEscape(exprToString(cc->getFirstExpr())) + "</code> cannot hold when key " + linkKey(b) + " is present.";
                    } else {
                        sentence = "These two conditions cannot both hold simultaneously.";
                    }
                }
                codeLine = "conflicts " + exprToString(cc->getFirstExpr()) + " with " + exprToString(cc->getSecondExpr()) + ";";
            } else if (c->getKind() == Constraint::Kind::Dependency) {
                auto dc = std::static_pointer_cast<DependencyConstraint>(c);
                kindBadge = "<span class=\"badge kind\">Requires</span>";
                title = "Dependency";
                std::string a, b;
                if (isIdentifierExpr(dc->getDependentExpr(), a)) {
                    if (isIdentifierExpr(dc->getCondition(), b)) {
                        sentence = "If key " + linkKey(a) + " is present, then key " + linkKey(b) + " must be present.";
                    } else {
                        sentence = "If key " + linkKey(a) + " is present, then <code>" + htmlEscape(exprToString(dc->getCondition())) + "</code> must hold.";
                    }
                } else {
                    if (isIdentifierExpr(dc->getCondition(), b)) {
                        sentence = "If <code>" + htmlEscape(exprToString(dc->getDependentExpr())) + "</code> holds, then key " + linkKey(b) + " must be present.";
                    } else {
                        sentence = "If <code>" + htmlEscape(exprToString(dc->getDependentExpr())) + "</code> holds, then <code>" + htmlEscape(exprToString(dc->getCondition())) + "</code> must hold.";
                    }
                }
                codeLine = "requires " + exprToString(dc->getDependentExpr()) + " => " + exprToString(dc->getCondition()) + ";";
            } else if (c->getKind() == Constraint::Kind::Validate) {
                auto vc = std::static_pointer_cast<ValidateConstraint>(c);
                kindBadge = "<span class=\"badge warn\">Validate</span>";
                title = "Validation";
                sentence = "The configuration must satisfy: <code>" + htmlEscape(exprToString(vc->getExpr())) + "</code>.";
                codeLine = "validate " + exprToString(vc->getExpr()) + ";";
            }

            out << "<div class=\"constraint\" style=\"margin:10px 0\">";
            out << "<div class=\"row\">";
            out << "<div class=\"title\">" << kindBadge << " " << htmlEscape(title) << "</div>";
            out << "<button class=\"iconbtn\" type=\"button\" data-copy=\"" << htmlEscape(codeLine) << "\">Copy rule</button>";
            out << "</div>";
            out << "<p>" << sentence << "</p>";
            out << "<pre><code>" << htmlEscape(codeLine) << "</code></pre>";
            out << "</div>";
        }

        out << "</div></div>";
    }

    // -------------------- Page renderers --------------------

    void renderSchemaRootPage(const std::shared_ptr<CSL::ConfigSchema>& schema) {
        const auto schemaName = schema->getName();
        auto root = schema->getRootTable();

        // collect known keys for constraint linking
        std::unordered_set<std::string> known;
        for (const auto& kd : root->getExplicitKeys()) known.insert(kd->getName());
        if (root->getWildcardKey()) known.insert("*");

        std::ostringstream main;

        // Overview card
        {
            std::ostringstream subtitle;
            subtitle << "<span class=\"meta\">Root</span>";

            main << "<div class=\"card\">";
            main << "<div class=\"cardhead\"><h1>Schema <code>" << htmlEscape(schemaName) << "</code></h1>";
            main << "<button class=\"iconbtn\" type=\"button\" data-copy-el=\"schema-src\">Copy schema</button>";
            main << "</div>";
            main << "<div class=\"cardbody\">";
            main << "<div class=\"kpis\">";
            main << "<div class=\"kpi\"><div class=\"k\">Top-level keys</div><div class=\"v\">" << countKeys(root) << "</div></div>";
            main << "<div class=\"kpi\"><div class=\"k\">Has wildcard</div><div class=\"v\">" << (root->getWildcardKey() ? "Yes" : "No") << "</div></div>";
            main << "<div class=\"kpi\"><div class=\"k\">Max depth</div><div class=\"v\">" << nestedDepth(root) << "</div></div>";
            main << "</div>";

            main << "<p class=\"meta\" style=\"margin-top:10px\">Full schema (as CSL):</p>";
            main << "<pre><code id=\"schema-src\">" << htmlEscape(CSL::toCsl(schema)) << "</code></pre>";
            main << "</div>";
            main << "</div>";
        }

        // Schema-wide structure graph
        main << renderSchemaGraphCard(schemaName, root);

        // Keys card
        {
            main << "<div class=\"card\">";
            main << "<div class=\"cardhead\">";
            main << "<h2>Keys</h2>";
            main << "<input class=\"filter\" placeholder=\"Filter keys (name, type, annotation…)\" data-filter-table=\"keys-table\" aria-label=\"Filter keys\">";
            main << "</div>";
            main << "<div class=\"cardbody\">";

            if (root->getWildcardKey()) {
                std::string dyn = dynamicKeyPlaceholder({});
                main << "<div class=\"callout\" style=\"margin-bottom:12px\">";
                main << "<strong>Dynamic keys:</strong> this table allows additional keys like <code>" << htmlEscape(dyn) << "</code>. ";
                main << "Explicit keys take precedence over wildcard rules.";
                main << "</div>";
            }

            renderKeysTable(schemaName, {}, root, main);
            main << "</div></div>";
        }

        // Constraints card (if any)
        renderConstraints(root, known, main);

        const std::string filename = schemaFileFor(schemaName);
        addPage(filename, pageWrap(schemaName, schemaName, filename, main.str()));
    }

    void renderTablePage(const std::shared_ptr<CSL::ConfigSchema>& schema, const TablePageMeta& meta) {
        const std::string schemaName = schema->getName();
        const std::string currentFile = meta.filename;

        // known keys for constraint linking
        std::unordered_set<std::string> known;
        for (const auto& kd : meta.table->getExplicitKeys()) known.insert(kd->getName());
        if (meta.table->getWildcardKey()) known.insert("*");

        std::ostringstream main;

        // Header card
        {
            main << "<div class=\"card\">";
            main << "<div class=\"cardhead\">";
            main << "<h1>Table <code>" << htmlEscape(displayPath(meta.path)) << "</code></h1>";
            main << "<a class=\"iconbtn\" href=\"" << htmlEscape(schemaFileFor(schemaName)) << "\">Back to schema</a>";
            main << "</div>";
            main << "<div class=\"cardbody\">";
            main << "<p class=\"meta\">Belongs to schema <code>" << htmlEscape(schemaName) << "</code> at path <code>" << htmlEscape(displayPath(meta.path)) << "</code>.</p>";
            main << "<div class=\"kpis\">";
            main << "<div class=\"kpi\"><div class=\"k\">Keys</div><div class=\"v\">" << countKeys(meta.table) << "</div></div>";
            main << "<div class=\"kpi\"><div class=\"k\">Has wildcard</div><div class=\"v\">" << (meta.table->getWildcardKey() ? "Yes" : "No") << "</div></div>";
            main << "</div>";
            main << "</div></div>";
        }

        // Structure graph card for this table
        main << renderTableGraphCard(schemaName, meta);

        // Keys card (+ wildcard callout)
        {
            main << "<div class=\"card\">";
            main << "<div class=\"cardhead\">";
            main << "<h2>Keys</h2>";
            main << "<input class=\"filter\" placeholder=\"Filter keys…\" data-filter-table=\"keys-table\" aria-label=\"Filter keys\">";
            main << "</div>";
            main << "<div class=\"cardbody\">";

            if (meta.table->getWildcardKey()) {
                std::vector<std::string> parentPath = meta.path;
                // wildcard placeholder should refer to the parent object (the path without the wildcard segment, if this table itself is already under wildcard)
                if (!parentPath.empty() && (parentPath.back() == "*" || parentPath.back() == "*[]")) parentPath.pop_back();

                std::string dyn = dynamicKeyPlaceholder(parentPath);
                main << "<div class=\"callout\" style=\"margin-bottom:12px\">";
                main << "<strong>Dynamic keys:</strong> this table allows additional keys like <code>" << htmlEscape(dyn) << "</code>. ";
                main << "Explicit keys take precedence over wildcard rules.";
                main << "</div>";
            }

            renderKeysTable(schemaName, meta.path, meta.table, main);
            main << "</div></div>";
        }

        // Constraints card
        renderConstraints(meta.table, known, main);

        addPage(currentFile, pageWrap(schemaName + " / " + displayPath(meta.path), schemaName, currentFile, main.str(), "", &meta.path));
    }

    void renderFullSchema(const std::shared_ptr<CSL::ConfigSchema>& schema) {
        const std::string schemaName = schema->getName();

        // plan nested pages first so sidebar knows everything
        planTablesForSchema(schemaName, schema->getRootTable());

        // render all nested table pages
        for (const auto& meta : planned[schemaName]) {
            renderTablePage(schema, meta);
        }

        // render schema root page
        renderSchemaRootPage(schema);
    }
};

std::unordered_map<std::string, std::string> toHtmlDoc(const std::shared_ptr<CSL::ConfigSchema>& schema) {
    HtmlPagesGen gen;

    // shared assets
    gen.addPage("site.css", siteCss());
    gen.addPage("site.js", siteJs());

    gen.renderFullSchema(schema);

    // index
    std::ostringstream main;
    main << "<div class=\"card\"><div class=\"cardhead\"><h1>CSL Documentation</h1></div><div class=\"cardbody\">";
    std::string schemaFile = schemaFileFor(schema->getName());
    main << "<p><a class=\"link\" href=\"" << htmlEscape(schemaFile) << "\">Open schema <code>" << htmlEscape(schema->getName()) << "</code></a></p>";
    main << "</div></div>";

    gen.addPage("index.html", gen.pageWrap("CSL Documentation", "", "index.html", main.str()));
    return gen.pages;
}

std::unordered_map<std::string, std::string> toHtmlDoc(const std::vector<std::shared_ptr<CSL::ConfigSchema>>& schemas) {
    HtmlPagesGen gen;

    // shared assets
    gen.addPage("site.css", siteCss());
    gen.addPage("site.js", siteJs());

    // render each schema (including its nested table pages)
    for (const auto& s : schemas) {
        gen.renderFullSchema(s);
    }

    // index listing
    std::ostringstream main;
    main << "<div class=\"card\"><div class=\"cardhead\"><h1>CSL Documentation</h1></div><div class=\"cardbody\">";
    main << "<p class=\"meta\">Schemas:</p>";
    main << "<ul>";
    for (const auto& s : schemas) {
        std::string schemaFile = schemaFileFor(s->getName());
        main << "<li><a class=\"link\" href=\"" << htmlEscape(schemaFile) << "\"><code>" << htmlEscape(s->getName()) << "</code></a></li>";
    }
    main << "</ul>";
    main << "</div></div>";

    gen.addPage("index.html", gen.pageWrap("CSL Documentation", "", "index.html", main.str()));
    return gen.pages;
}

} // namespace CSL
