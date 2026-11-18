#include "parse.h"

#include "normalize.h"

#include <nlohmann/json.hpp>
#include <pg_query.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <initializer_list>
#include <map>
#include <sstream>

namespace schemasync {

using json = nlohmann::json;

namespace {

class ParseTree {
public:
    explicit ParseTree(const std::string& sql) : result_(pg_query_parse(sql.c_str())) {}
    ~ParseTree() { pg_query_free_parse_result(result_); }
    ParseTree(const ParseTree&) = delete;
    ParseTree& operator=(const ParseTree&) = delete;

    const PgQueryError* error() const { return result_.error; }
    json tree() const { return json::parse(result_.parse_tree); }

private:
    PgQueryParseResult result_;
};

const json& list(const json& node, const char* key) {
    static const json empty = json::array();
    auto it = node.find(key);
    return it == node.end() ? empty : *it;
}

std::string str(const json& node, const char* key) { return node.value(key, std::string{}); }

int location(const json& node) { return node.value("location", 0); }

std::vector<std::string> strings(const json& items) {
    std::vector<std::string> out;
    for (const auto& item : items) out.push_back(item.at("String").value("sval", std::string{}));
    return out;
}

std::optional<std::string> first_present(const json& node, std::initializer_list<const char*> keys) {
    for (const char* k : keys)
        if (node.contains(k)) return k;
    return std::nullopt;
}

std::string pretty(const std::string& enum_name) {
    auto pos = enum_name.find('_');
    return pos == std::string::npos ? enum_name : enum_name.substr(pos + 1);
}

std::string quote_literal(std::string_view s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += '\'';
        out += c;
    }
    return out + "'";
}

struct TypeSpec {
    std::string name;
    std::vector<int> mods;
    bool array = false;
};

// ponytail: the schema qualifier of a type name is dropped; types resolve by name only.
std::optional<TypeSpec> type_spec(const json& tn) {
    auto names = strings(list(tn, "names"));
    if (names.empty() || tn.value("setof", false) || tn.value("pct_type", false)) return std::nullopt;
    TypeSpec spec{names.back(), {}, !list(tn, "arrayBounds").empty()};
    for (const auto& m : list(tn, "typmods")) {
        auto c = m.find("A_Const");
        if (c == m.end() || !c->contains("ival")) return std::nullopt;
        spec.mods.push_back(c->at("ival").value("ival", 0));
    }
    return spec;
}

struct Expr {
    std::string sql;
    std::string key;
};

std::optional<Expr> constant(const json& c) {
    if (c.value("isnull", false)) return Expr{"NULL", "=NULL"};
    if (auto it = c.find("ival"); it != c.end()) {
        auto v = std::to_string(it->value("ival", 0));
        return Expr{v, "=" + v};
    }
    if (auto it = c.find("fval"); it != c.end()) {
        auto v = it->value("fval", std::string{});
        return Expr{v, "=" + v};
    }
    if (auto it = c.find("boolval"); it != c.end()) {
        std::string v = it->value("boolval", false) ? "true" : "false";
        return Expr{v, "=" + v};
    }
    if (auto it = c.find("sval"); it != c.end()) {
        auto v = it->value("sval", std::string{});
        return Expr{quote_literal(v), "=" + v};
    }
    return std::nullopt;
}

// Supported default expressions: constants, casts, function calls and SQL value functions.
// The key strips casts of constants and maps now()/transaction_timestamp() to CURRENT_TIMESTAMP,
// so source text and pg_get_expr output compare equal.
std::optional<Expr> expr(const json& node) {
    if (auto it = node.find("A_Const"); it != node.end()) return constant(*it);

    if (auto it = node.find("TypeCast"); it != node.end()) {
        auto arg = expr(it->at("arg"));
        auto spec = type_spec(it->at("typeName"));
        if (!arg || !spec) return std::nullopt;
        auto type = make_type(spec->name, spec->mods, spec->array);
        if (!type) return std::nullopt;
        const auto t = render_type(*type);
        const bool of_constant = it->at("arg").contains("A_Const");
        return Expr{arg->sql + "::" + t, of_constant ? arg->key : arg->key + "::" + t};
    }

    if (auto it = node.find("FuncCall"); it != node.end()) {
        const auto& f = *it;
        if (first_present(f, {"agg_order", "agg_filter", "over", "agg_star", "agg_distinct", "func_variadic"}))
            return std::nullopt;
        const auto parts = strings(list(f, "funcname"));
        std::string sql;
        for (const auto& p : parts) sql += (sql.empty() ? "" : ".") + p;
        std::string arg_sql;
        std::string arg_key;
        for (const auto& a : list(f, "args")) {
            auto e = expr(a);
            if (!e) return std::nullopt;
            arg_sql += (arg_sql.empty() ? "" : ", ") + e->sql;
            arg_key += (arg_key.empty() ? "" : ",") + e->key;
        }
        const auto& name = parts.back();
        const bool is_now = (name == "now" || name == "transaction_timestamp") && arg_sql.empty();
        return Expr{sql + "(" + arg_sql + ")", is_now ? "CURRENT_TIMESTAMP" : name + "(" + arg_key + ")"};
    }

    if (auto it = node.find("SQLValueFunction"); it != node.end()) {
        auto op = str(*it, "op");
        if (!op.starts_with("SVFOP_")) return std::nullopt;
        auto name = op.substr(6);
        if (name.ends_with("_N")) name = std::format("{}({})", name.substr(0, name.size() - 2), it->value("typmod", -1));
        return Expr{name, name};
    }
    return std::nullopt;
}

std::optional<Default> make_default(const Expr& e) {
    if (e.key == "=NULL") return std::nullopt;
    return Default{e.sql, e.key};
}

class Parser {
public:
    Parser(Schema& schema, const std::string& sql, const std::string& file, const std::string& managed)
        : schema_(schema), sql_(sql), file_(file), managed_(managed) {}

    Result<void> run() {
        ParseTree tree(sql_);
        if (const auto* e = tree.error())
            return std::unexpected(error_at(e->cursorpos > 0 ? e->cursorpos - 1 : 0, e->message, ErrorKind::parse));
        try {
            const auto root = tree.tree();
            for (const auto& raw : list(root, "stmts"))
                if (auto r = statement(raw.at("stmt"), skip_blank(raw.value("stmt_location", 0))); !r) return r;
        } catch (const json::exception& e) {
            return std::unexpected(Error{ErrorKind::parse, std::format("unexpected parse tree: {}", e.what()), "", file_});
        }
        return {};
    }

private:
    Error error_at(int offset, std::string message, ErrorKind kind = ErrorKind::unsupported) const {
        int line = 1;
        int col = 1;
        for (int i = 0; i < offset && i < static_cast<int>(sql_.size()); ++i) {
            if (sql_[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        return Error{kind, std::move(message), "", file_, line, col};
    }

    std::unexpected<Error> fail(int offset, std::string message) const {
        return std::unexpected(error_at(offset, std::move(message)));
    }

    int skip_blank(int offset) const {
        const auto n = static_cast<int>(sql_.size());
        while (offset < n) {
            if (std::isspace(static_cast<unsigned char>(sql_[offset]))) {
                ++offset;
            } else if (sql_.compare(offset, 2, "--") == 0) {
                while (offset < n && sql_[offset] != '\n') ++offset;
            } else if (sql_.compare(offset, 2, "/*") == 0) {
                auto end = sql_.find("*/", offset + 2);
                offset = end == std::string::npos ? n : static_cast<int>(end) + 2;
            } else {
                break;
            }
        }
        return offset;
    }

    Result<QualifiedName> relation(const json& rv) const {
        const auto schema = str(rv, "schemaname");
        if (!schema.empty() && schema != managed_)
            return fail(location(rv), std::format("schema \"{}\" is not managed by this run (--schema {})", schema, managed_));
        if (!str(rv, "catalogname").empty()) return fail(location(rv), "database-qualified names are not supported");
        if (str(rv, "relpersistence") != "p")
            return fail(location(rv), "temporary and unlogged tables are not supported");
        return QualifiedName{managed_, str(rv, "relname")};
    }

    Result<void> statement(const json& stmt, int offset) {
        const auto& kind = stmt.begin().key();
        const auto& node = stmt.begin().value();
        if (kind == "CreateStmt") return create_table(node, offset);
        if (kind == "IndexStmt") return create_index(node, offset);
        if (kind == "AlterTableStmt") return alter_table(node, offset);
        return fail(offset, std::format("unsupported statement {} (schemasync manages tables, columns, constraints and indexes)", kind));
    }

    Result<void> create_table(const json& node, int offset) {
        if (auto k = first_present(node, {"inhRelations", "partspec", "partbound", "ofTypename", "accessMethod", "options",
                                          "tablespacename"}))
            return fail(offset, std::format("unsupported CREATE TABLE clause ({})", *k));
        auto name = relation(node.at("relation"));
        if (!name) return std::unexpected(name.error());
        if (schema_.tables.contains(*name)) return fail(offset, std::format("table {} is defined twice", name->name));

        Table table{*name};
        for (const auto& elt : list(node, "tableElts")) {
            if (auto cd = elt.find("ColumnDef"); cd != elt.end()) {
                if (auto r = column_def(table, *cd); !r) return r;
            } else if (auto con = elt.find("Constraint"); con != elt.end()) {
                if (auto r = constraint(table, *con, {}); !r) return r;
            } else {
                return fail(offset, std::format("unsupported table element {}", elt.begin().key()));
            }
        }
        schema_.tables.emplace(table.name, std::move(table));
        return {};
    }

    Result<void> column_def(Table& table, const json& cd) {
        const int offset = location(cd);
        Column col{str(cd, "colname")};
        if (table.column(col.name)) return fail(offset, std::format("column \"{}\" is defined twice", col.name));
        if (auto k = first_present(cd, {"collClause", "compression", "storage_name", "identity", "generated", "raw_default",
                                        "cooked_default", "fdwoptions"}))
            return fail(offset, std::format("unsupported column option ({})", *k));

        auto spec = type_spec(cd.at("typeName"));
        if (!spec) return fail(offset, "unsupported column type");
        static const std::map<std::string, std::string> serials{
            {"serial", "int4"}, {"serial4", "int4"}, {"bigserial", "int8"},
            {"serial8", "int8"}, {"smallserial", "int2"}, {"serial2", "int2"}};
        const auto serial = serials.find(spec->name);
        if (serial != serials.end()) {
            if (!spec->mods.empty() || spec->array) return fail(offset, "serial types take no modifiers");
            spec->name = serial->second;
        }
        auto type = make_type(spec->name, spec->mods, spec->array);
        if (!type) return fail(offset, type.error().message);
        col.type = *type;

        for (const auto& wrapped : list(cd, "constraints")) {
            const auto& con = wrapped.at("Constraint");
            const auto kind = str(con, "contype");
            if (kind == "CONSTR_NOTNULL") {
                col.not_null = true;
            } else if (kind == "CONSTR_NULL") {
                col.not_null = false;
            } else if (kind == "CONSTR_DEFAULT") {
                auto e = expr(con.at("raw_expr"));
                if (!e)
                    return fail(location(con), "unsupported DEFAULT expression (use a constant, a cast, a function call or CURRENT_TIMESTAMP)");
                col.default_value = make_default(*e);
            } else if (kind == "CONSTR_PRIMARY" || kind == "CONSTR_UNIQUE" || kind == "CONSTR_FOREIGN") {
                if (auto r = constraint(table, con, {col.name}); !r) return r;
            } else {
                return fail(location(con), std::format("unsupported column constraint {}", pretty(kind)));
            }
        }
        if (serial != serials.end()) {
            if (col.default_value) return fail(offset, "serial columns cannot have a DEFAULT");
            col.not_null = true;
            col.default_value = serial_default(table.name.name, col.name);
        }
        table.columns.push_back(std::move(col));
        return {};
    }

    Result<void> constraint(Table& table, const json& con, const std::vector<std::string>& implied) {
        const int offset = location(con);
        const auto kind = str(con, "contype");
        if (auto k = first_present(con, {"deferrable", "initdeferred", "skip_validation", "including", "options", "indexname",
                                         "indexspace", "nulls_not_distinct", "fk_del_set_cols", "where_clause",
                                         "without_overlaps", "fk_with_period", "pk_with_period", "exclusions"}))
            return fail(offset, std::format("unsupported constraint option ({})", *k));

        const auto name = str(con, "conname");
        const auto& t = table.name.name;
        auto columns = [&](const char* key) { return implied.empty() ? strings(list(con, key)) : implied; };

        if (kind == "CONSTR_PRIMARY") {
            if (table.primary_key) return fail(offset, std::format("table {} has more than one primary key", t));
            table.primary_key = KeyConstraint{name.empty() ? make_object_name(t, "", "pkey") : name, columns("keys")};
        } else if (kind == "CONSTR_UNIQUE") {
            auto cols = columns("keys");
            KeyConstraint key{name.empty() ? make_object_name(t, join_names(cols), "key") : name, cols};
            if (!table.uniques.emplace(key.name, key).second)
                return fail(offset, std::format("constraint {} is defined twice", key.name));
        } else if (kind == "CONSTR_FOREIGN") {
            if (!con.value("is_enforced", false)) return fail(offset, "NOT ENFORCED foreign keys are not supported");
            if (str(con, "fk_matchtype") != "s") return fail(offset, "only MATCH SIMPLE foreign keys are supported");
            auto ref = relation(con.at("pktable"));
            if (!ref) return std::unexpected(ref.error());
            auto cols = columns("fk_attrs");
            auto action = [&](const char* key) {
                auto a = str(con, key);
                return a.empty() ? 'a' : a[0];
            };
            ForeignKey fk{name.empty() ? make_object_name(t, join_names(cols), "fkey") : name,
                          cols,
                          *ref,
                          strings(list(con, "pk_attrs")),
                          action("fk_del_action"),
                          action("fk_upd_action")};
            if (!table.foreign_keys.emplace(fk.name, fk).second)
                return fail(offset, std::format("constraint {} is defined twice", fk.name));
        } else {
            return fail(offset, std::format("unsupported constraint {}", pretty(kind)));
        }
        return {};
    }

    Result<void> create_index(const json& node, int offset) {
        if (auto k = first_present(node, {"whereClause", "indexIncludingParams", "options", "tableSpace", "nulls_not_distinct",
                                          "primary", "isconstraint", "excludeOpNames"}))
            return fail(offset, std::format("unsupported CREATE INDEX clause ({})", *k));
        if (str(node, "accessMethod") != "btree") return fail(offset, "only btree indexes are supported");
        auto table = relation(node.at("relation"));
        if (!table) return std::unexpected(table.error());

        Index index{{managed_, ""}, *table, {}, node.value("unique", false)};
        for (const auto& p : list(node, "indexParams")) {
            const auto& e = p.at("IndexElem");
            if (first_present(e, {"expr", "collation", "opclass", "opclassopts"}) ||
                str(e, "ordering") != "SORTBY_DEFAULT" || str(e, "nulls_ordering") != "SORTBY_NULLS_DEFAULT")
                return fail(offset, "only plain column indexes are supported (no expressions, ordering, collations or operator classes)");
            index.columns.push_back(str(e, "name"));
        }
        const auto name = str(node, "idxname");
        index.name.name = name.empty() ? make_object_name(table->name, join_names(index.columns), "idx") : name;
        if (!schema_.indexes.emplace(index.name, index).second)
            return fail(offset, std::format("index {} is defined twice", index.name.name));
        return {};
    }

    Result<void> alter_table(const json& node, int offset) {
        if (str(node, "objtype") != "OBJECT_TABLE") return fail(offset, "only ALTER TABLE is supported");
        auto name = relation(node.at("relation"));
        if (!name) return std::unexpected(name.error());
        auto it = schema_.tables.find(*name);
        if (it == schema_.tables.end())
            return fail(offset, std::format("ALTER TABLE {}: the table must be created first (files are read in alphabetical order)", name->name));
        for (const auto& cmd : list(node, "cmds")) {
            const auto& c = cmd.at("AlterTableCmd");
            if (str(c, "subtype") != "AT_AddConstraint")
                return fail(offset, std::format("unsupported ALTER TABLE action {} (only ADD CONSTRAINT)", pretty(str(c, "subtype"))));
            if (auto r = constraint(it->second, c.at("def").at("Constraint"), {}); !r) return r;
        }
        return {};
    }

    Schema& schema_;
    const std::string& sql_;
    const std::string& file_;
    const std::string& managed_;
};

}  // namespace

Result<Schema> parse_string(const std::string& sql, const std::string& schema, const std::string& file) {
    Schema out;
    if (auto r = Parser(out, sql, file, schema).run(); !r) return std::unexpected(r.error());
    if (auto r = finalize(out); !r) return std::unexpected(r.error());
    return out;
}

Result<Schema> load_schema_files(const std::filesystem::path& dir, const std::string& schema) {
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        if (entry.is_regular_file() && entry.path().extension() == ".sql") files.push_back(entry.path());
    if (ec) return std::unexpected(Error{ErrorKind::io, std::format("cannot read {}: {}", dir.string(), ec.message())});
    std::ranges::sort(files);

    Schema out;
    for (const auto& path : files) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::unexpected(Error{ErrorKind::io, std::format("cannot open {}", path.string())});
        std::stringstream buffer;
        buffer << in.rdbuf();
        auto sql = buffer.str();
        if (sql.starts_with("\xEF\xBB\xBF")) sql.erase(0, 3);
        const auto file = path.string();
        if (auto r = Parser(out, sql, file, schema).run(); !r) return std::unexpected(r.error());
    }
    if (auto r = finalize(out); !r) return std::unexpected(r.error());
    return out;
}

std::optional<Default> parse_default_expr(const std::string& text) {
    ParseTree tree("SELECT " + text);
    if (!tree.error()) {
        try {
            const auto root = tree.tree();
            const auto& stmts = list(root, "stmts");
            if (stmts.size() == 1) {
                const auto& targets = list(stmts[0].at("stmt").at("SelectStmt"), "targetList");
                if (targets.size() == 1)
                    if (auto e = expr(targets[0].at("ResTarget").at("val"))) return make_default(*e);
            }
        } catch (const json::exception&) {
        }
    }
    return Default{text, text};
}

ParserGuard::~ParserGuard() { pg_query_exit(); }

}  // namespace schemasync
