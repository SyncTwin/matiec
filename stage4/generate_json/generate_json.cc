/*
 *  matiec - a compiler for the programming languages defined in IEC 61131-3
 *
 *  Copyright (C) 2003-2011  Mario de Sousa (msousa@fe.up.pt)
 *  Copyright (C) 2007-2011  Laurent Bessard and Edouard Tisserant
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * This code is made available on the understanding that it will not be
 * used in safety-critical situations without a full and competent review.
 */

/*
 * An IEC 61131-3 compiler.
 *
 * Based on the
 * FINAL DRAFT - IEC 61131-3, 2nd Ed. (2001-12-10)
 *
 */


/*
 * A 4th stage that, instead of generating code, prints a JSON model
 * of the (semantically verified) IEC 61131-3 source program:
 *
 *   { "pous": [ {name, kind, vars[], sfc?, body_st?, body_il?, pragmas[]} ],
 *     "configuration": {name, globals[], resources: [{name, type, globals[], tasks[], instances[]}]},
 *     "configurations": [ ... only when the source has more than one CONFIGURATION ... ],
 *     "source_map": { "<pou>/var/<name>": "file:line:col", ... } }
 *
 * POUs placed between {disable code generation} .. {enable code generation}
 * (i.e. the standard library loaded by the compiler itself) are not printed.
 *
 * ST/IL text (bodies, transition conditions, initial values, types) is
 * printed by re-using the generate_iec_c visitor of the iec2iec generator.
 *
 * The JSON is written by hand (no external dependencies). Strings are
 * escaped as required by RFC 8259; the source text is assumed to be UTF-8
 * (or plain ASCII) and is copied byte for byte.
 */


#include <string>
#include <vector>
#include <sstream>
#include <typeinfo>

/* Re-use the IEC 61131-3 printer, without its stage4 entry points. */
#define GENERATE_IEC_NO_STAGE4_ENTRY_POINTS
#include "../generate_iec/generate_iec.cc"
#undef  GENERATE_IEC_NO_STAGE4_ENTRY_POINTS

#include "generate_json.hh"
#include "../../stage1_2/stage1_2.hh"  // for get_var_decl_pragmas()
#include "../../absyntax_utils/absyntax_utils.hh"  // for search_base_type_c, get_datatype_info_c



/***********************************************************************/
/***********************************************************************/

/* Parse command line options passed from main.c !! */

int  stage4_parse_options(char *options) {return 0;}

void stage4_print_options(void) {
  printf("          (no options available when generating the JSON model)\n");
}



/***********************************************************************/
/***********************************************************************/
/*  A minimal JSON value, printed with indentation.                    */
/***********************************************************************/
/***********************************************************************/

class json_value_c {
  public:
    typedef enum {j_raw, j_string, j_array, j_object} kind_t;

  private:
    kind_t kind;
    std::string value;                    /* j_raw: literal JSON text (number, true, null); j_string: unescaped text */
    std::vector<std::string>  keys;       /* j_object only */
    std::vector<json_value_c> items;      /* j_array and j_object */

  public:
    json_value_c(kind_t kind_ = j_object, std::string value_ = ""): kind(kind_), value(value_) {}

    static json_value_c str (std::string s) {return json_value_c(j_string, s);}
    static json_value_c raw (std::string s) {return json_value_c(j_raw, s);}
    static json_value_c boolean(bool b)     {return json_value_c(j_raw, b?"true":"false");}
    static json_value_c array(void)         {return json_value_c(j_array);}
    static json_value_c object(void)        {return json_value_c(j_object);}

    json_value_c &set(std::string key, const json_value_c &v) {keys.push_back(key); items.push_back(v); return *this;}
    json_value_c &push(const json_value_c &v)                {items.push_back(v); return *this;}
    size_t size(void) {return items.size();}

    static std::string escape(const std::string &s) {
      std::string res = "\"";
      for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = s[i];
        switch (c) {
          case '"':  res += "\\\""; break;
          case '\\': res += "\\\\"; break;
          case '\b': res += "\\b";  break;
          case '\f': res += "\\f";  break;
          case '\n': res += "\\n";  break;
          case '\r': res += "\\r";  break;
          case '\t': res += "\\t";  break;
          default:
            if (c < 0x20) {
              char buf[8];
              snprintf(buf, sizeof(buf), "\\u%04x", c);
              res += buf;
            } else
              res += (char)c;
        }
      }
      return res + "\"";
    }

    void print(std::ostream &out, std::string indent = "") const {
      std::string inner = indent + "  ";
      switch (kind) {
        case j_raw:    out << value; return;
        case j_string: out << escape(value); return;
        case j_array:
        case j_object:
          out << ((kind == j_array)?"[":"{");
          if (items.empty()) {out << ((kind == j_array)?"]":"}"); return;}
          for (size_t i = 0; i < items.size(); i++) {
            out << ((i == 0)?"\n":",\n") << inner;
            if (kind == j_object) out << escape(keys[i]) << ": ";
            items[i].print(out, inner);
          }
          out << "\n" << indent << ((kind == j_array)?"]":"}");
          return;
      }
    }
};



/***********************************************************************/
/***********************************************************************/
/*  Helpers                                                            */
/***********************************************************************/
/***********************************************************************/

/* a stage4out_c that writes into a string, so we can re-use generate_iec_c */
class stage4out_string_c: public stage4out_c {
  private:
    std::ostringstream buf;
  public:
    stage4out_string_c(void): stage4out_c("  ") {out = &buf;}
    std::string str(void) {return buf.str();}
};


static std::string trim(const std::string &s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}


/* generate_iec_c, with a few changes so that the printed text is plain IEC 61131-3 source */
class generate_iec_text_c: public generate_iec_c {
  private:
    stage4out_c &out;
  public:
    generate_iec_text_c(stage4out_c *s4o_ptr): generate_iec_c(s4o_ptr), out(*s4o_ptr) {}
    void *visit(boolean_true_c *symbol)  {return out.print("TRUE");}
    void *visit(boolean_false_c *symbol) {return out.print("FALSE");}
    /* the lexical analyser strips the enclosing braces of the pragmas */
    void *visit(pragma_c *symbol)        {out.print("{"); out.print(symbol->value); return out.print("}");}
    /* as generate_iec_c, but a pragma inside a statement list is not a statement: no ';' after it */
    void *visit(statement_list_c *symbol) {
      for (int i = 0; i < symbol->n; i++) {
        symbol_c *element = symbol->get_element(i);
        out.print(out.indent_spaces);
        element->accept(*this);
        out.print((NULL != dynamic_cast<pragma_c *>(element))? "\n" : ";\n");
      }
      return NULL;
    }
};


/* IEC 61131-3 text of an AST sub-tree (NULL -> "") */
static std::string iec_text(symbol_c *symbol) {
  if (NULL == symbol) return "";
  stage4out_string_c s4o_str;
  generate_iec_text_c printer(&s4o_str);
  symbol->accept(printer);
  return trim(s4o_str.str());
}


/* "file:line:col" of a symbol */
static std::string src_loc(symbol_c *symbol) {
  if (NULL == symbol) return "";
  std::ostringstream res;
  res << ((NULL != symbol->first_file)? symbol->first_file : "") << ":" << symbol->first_line << ":" << symbol->first_column;
  return res.str();
}


/* An integer literal as a JSON number (e.g. "1_000" -> 1000); anything else as a JSON string. */
static json_value_c json_integer(symbol_c *symbol) {
  std::string txt = iec_text(symbol), digits;
  for (size_t i = 0; i < txt.size(); i++) {
    if (txt[i] == '_') continue;
    if (!isdigit((unsigned char)txt[i]) && !(i == 0 && (txt[i] == '-' || txt[i] == '+'))) return json_value_c::str(txt);
    if (txt[i] != '+') digits += txt[i];
  }
  if (digits.empty() || digits == "-") return json_value_c::str(txt);
  return json_value_c::raw(digits);
}


/* Pragma text as written in the source: the lexer strips the enclosing '{' '}' of library and body pragmas. */
static std::string pragma_text(pragma_c *symbol) {
  return std::string("{") + symbol->value + "}";
}


/* Split a '<type> [:= <init>]' node into its type and its initial value. */
static void split_spec_init(symbol_c *spec_init, symbol_c *&type, symbol_c *&init) {
  type = spec_init; init = NULL;
  #define SPLIT(class_name, type_field, init_field)                                   \
    if (NULL != dynamic_cast<class_name *>(spec_init)) {                               \
      type = dynamic_cast<class_name *>(spec_init)->type_field;                        \
      init = dynamic_cast<class_name *>(spec_init)->init_field;                        \
    } else
  SPLIT(simple_spec_init_c,          simple_specification,     constant)
  SPLIT(subrange_spec_init_c,        subrange_specification,   signed_integer)
  SPLIT(enumerated_spec_init_c,      enumerated_specification, enumerated_value)
  SPLIT(array_spec_init_c,           array_specification,      array_initialization)
  SPLIT(initialized_structure_c,     structure_type_name,      structure_initialization)
  SPLIT(fb_spec_init_c,              function_block_type_name, structure_initialization)
  SPLIT(ref_spec_init_c,             ref_spec,                 ref_initialization)
  SPLIT(single_byte_string_spec_c,   string_spec,              single_byte_character_string)
  SPLIT(double_byte_string_spec_c,   string_spec,              double_byte_character_string)
  {/* any other node: no initial value */}
  #undef SPLIT
}


/* A number as JSON text: an integer when it has no fractional part. */
static std::string json_number(long double value) {
  char buf[64];
  if ((value == (long double)(long long)value) && (value < 9e18L) && (value > -9e18L))
    snprintf(buf, sizeof(buf), "%lld", (long long)value);
  else
    snprintf(buf, sizeof(buf), "%.15Lg", value);
  return buf;
}


/* The value of a TIME literal (T#1m30s, TIME#-2.5ms, ...) in milliseconds, as JSON number text;
 * "" if 'symbol' is not a TIME literal.
 */
static std::string duration_ms(symbol_c *symbol) {
  duration_c *duration = dynamic_cast<duration_c *>(symbol);
  if (NULL == duration) return "";
  interval_c *interval = dynamic_cast<interval_c *>(duration->interval);
  if (NULL == interval) return "";
  symbol_c   *fields[5] = {interval->days, interval->hours, interval->minutes, interval->seconds, interval->milliseconds};
  long double factor[5] = {86400000.0L,    3600000.0L,      60000.0L,          1000.0L,           1.0L};
  long double total = 0;
  for (int i = 0; i < 5; i++) {
    if (NULL == fields[i]) continue;
    token_c *token = dynamic_cast<token_c *>(fields[i]);
    if (NULL == token) return "";
    std::string digits;
    for (const char *c = token->value; *c != '\0'; c++) if (*c != '_') digits += *c;
    total += strtold(digits.c_str(), NULL) * factor[i];
  }
  if (NULL != duration->neg) total = -total;
  return json_number(total);
}


/* The data type as resolved by stage 3 (through all the TYPE aliases): the elementary type
 * (INT, TIME, ...), or the name of the derived type/FB it resolves to, or the text of an anonymous type.
 */
static std::string base_type_name(symbol_c *type) {
  if (NULL == type) return "";
  symbol_c *base = search_base_type_c::get_basetype_decl(type);
  if (NULL == base) return iec_text(type);
  if (get_datatype_info_c::is_ANY_ELEMENTARY(base)) return iec_text(base);
  symbol_c *base_id = search_base_type_c::get_basetype_id(type);
  if (NULL != base_id) return iec_text(base_id);
  return iec_text(base);
}

/* The kind of a data type, as resolved by stage 3. */
static std::string type_kind(symbol_c *type) {
  if (NULL == type) return "unknown";
  if (NULL != dynamic_cast<single_byte_string_spec_c *>(type)) return "string";
  if (NULL != dynamic_cast<double_byte_string_spec_c *>(type)) return "string";
  if (NULL != dynamic_cast<string_type_declaration_c *>(type)) return "string";
  symbol_c *base = search_base_type_c::get_basetype_decl(type);
  if (NULL == base)                                     return "unknown";
  if (get_datatype_info_c::is_function_block(type))     return "function_block";
  if (get_datatype_info_c::is_subrange(type))           return "subrange";
  if (get_datatype_info_c::is_enumerated(type))         return "enumerated";
  if (get_datatype_info_c::is_array(type))              return "array";
  if (get_datatype_info_c::is_structure(type))          return "structure";
  if (get_datatype_info_c::is_ref_to(type))             return "ref";
  if (NULL != dynamic_cast<string_type_declaration_c *>(base)) return "string";
  if (get_datatype_info_c::is_ANY_ELEMENTARY(base))     return "elementary";
  return "unknown";
}


/* type, base_type, type_kind, [init, init_ms] of a variable, structure element or TYPE */
static void set_type_and_init(json_value_c &v, symbol_c *type, symbol_c *init, const char *type_key = "type") {
  v.set(type_key,    json_value_c::str(iec_text(type)));
  v.set("base_type", json_value_c::str(base_type_name(type)));
  v.set("type_kind", json_value_c::str(type_kind(type)));
  if (NULL != init) {
    v.set("init", json_value_c::str(iec_text(init)));
    std::string ms = duration_ms(init);
    if (!ms.empty()) v.set("init_ms", json_value_c::raw(ms));
  }
}




/***********************************************************************/
/***********************************************************************/
/*  The generator                                                      */
/***********************************************************************/
/***********************************************************************/

class generate_json_c: public iterator_visitor_c {
  private:
    stage4out_c &s4o;

    json_value_c pous, configurations, source_map;

    /* state while walking the library */
    bool          code_generation_enabled;
    json_value_c  pending_pragmas;   /* library-level pragmas preceding the next POU */

    /* state while walking a POU */
    std::string   pou_name;
    json_value_c *vars;              /* where to append variables (POU vars, or configuration/resource globals) */
    std::string   var_class;
    std::string   var_option;
    long int      pou_first_order;
    long int      prev_decl_last_order;
    size_t        next_var_pragma;   /* index into get_var_decl_pragmas() */

    /* state while walking an SFC */
    json_value_c  sfc_steps, sfc_transitions, sfc_actions;
    int           transition_count;

    /* state while walking a resource */
    json_value_c  res_tasks, res_instances;

  public:
    generate_json_c(stage4out_c *s4o_ptr): s4o(*s4o_ptr) {
      pous           = json_value_c::array();
      configurations = json_value_c::array();
      source_map     = json_value_c::object();
      code_generation_enabled = true;
      pending_pragmas = json_value_c::array();
      vars = NULL;
      pou_first_order = prev_decl_last_order = -1;
      next_var_pragma = 0;
      transition_count = 0;
    }
    ~generate_json_c(void) {}


  private:
    void map_src(std::string key, symbol_c *symbol) {source_map.set(key, json_value_c::str(src_loc(symbol)));}

    /* Pragmas written inside VAR .. END_VAR right before declaration 'decl'. They are kept by the
     * lexical analyser in a side table (they are not part of the grammar), ordered by token order.
     */
    json_value_c var_pragmas(symbol_c *decl) {
      json_value_c res = json_value_c::array();
      const std::vector<var_decl_pragma_t> &table = get_var_decl_pragmas();
      while ((next_var_pragma < table.size()) && (table[next_var_pragma].order < decl->first_order)) {
        const var_decl_pragma_t &p = table[next_var_pragma++];
        if ((p.order > pou_first_order) && (p.order > prev_decl_last_order))
          res.push(json_value_c::str(p.text));
      }
      prev_decl_last_order = decl->last_order;
      return res;
    }

    void add_var(symbol_c *name, symbol_c *type, symbol_c *init, symbol_c *location, const json_value_c &pragmas, std::string edge = "") {
      if (NULL == vars) return;
      json_value_c v = json_value_c::object();
      std::string var_name = iec_text(name);
      v.set("name",  json_value_c::str(var_name));
      v.set("class", json_value_c::str(var_class));
      set_type_and_init(v, type, init);
      if (NULL != location)    v.set("location", json_value_c::str(iec_text(location)));
      if (!var_option.empty()) v.set("option",   json_value_c::str(var_option));
      if (!edge.empty())       v.set("edge",     json_value_c::str(edge));
      v.set("pragmas", pragmas);
      vars->push(v);
      map_src(pou_name + "/var/" + var_name, name);
    }

    /* every element of a var1_list / fb_name_list / global_var_list with the same type and init */
    void add_var_list(symbol_c *decl, symbol_c *name_list, symbol_c *type, symbol_c *init, std::string edge = "") {
      json_value_c pragmas = var_pragmas(decl);
      list_c *list = dynamic_cast<list_c *>(name_list);
      if (NULL == list) {add_var(name_list, type, init, NULL, pragmas, edge); return;}
      for (int i = 0; i < list->n; i++)
        if (NULL == dynamic_cast<extensible_input_parameter_c *>(list->get_element(i)))
          add_var(list->get_element(i), type, init, NULL, pragmas, edge);
    }

    void *var_block(const char *class_name, symbol_c *option, symbol_c *decl_list) {
      var_class  = class_name;
      var_option = iec_text(option);
      if (NULL != decl_list) decl_list->accept(*this);
      var_option = "";
      return NULL;
    }

    void begin_pou(symbol_c *symbol, symbol_c *name) {
      pou_name = iec_text(name);
      pou_first_order = symbol->first_order;
      prev_decl_last_order = symbol->first_order;
      map_src(pou_name, symbol);
    }

    json_value_c take_pending_pragmas(void) {
      json_value_c res = pending_pragmas;
      pending_pragmas = json_value_c::array();
      return res;
    }

    /* Print a POU: FUNCTION, FUNCTION_BLOCK or PROGRAM */
    void *print_pou(symbol_c *symbol, const char *kind, symbol_c *name, symbol_c *return_type, symbol_c *var_declarations, symbol_c *body) {
      json_value_c pou_vars = json_value_c::array();
      json_value_c pou      = json_value_c::object();

      begin_pou(symbol, name);
      pou.set("name", json_value_c::str(pou_name));
      pou.set("kind", json_value_c::str(kind));
      if (NULL != return_type) {
        pou.set("return_type",      json_value_c::str(iec_text(return_type)));
        pou.set("return_base_type", json_value_c::str(base_type_name(return_type)));
      }

      vars = &pou_vars;
      if (NULL != var_declarations) var_declarations->accept(*this);
      vars = NULL;
      pou.set("vars", pou_vars);

      if (NULL != dynamic_cast<sequential_function_chart_c *>(body)) {
        sfc_steps       = json_value_c::array();
        sfc_transitions = json_value_c::array();
        sfc_actions     = json_value_c::array();
        transition_count = 0;
        body->accept(*this);
        json_value_c sfc = json_value_c::object();
        sfc.set("steps",       sfc_steps);
        sfc.set("transitions", sfc_transitions);
        sfc.set("actions",     sfc_actions);
        pou.set("sfc", sfc);
      } else if (NULL != dynamic_cast<instruction_list_c *>(body)) {
        pou.set("body_il", json_value_c::str(iec_text(body)));
      } else if (NULL != body) {
        pou.set("body_st", json_value_c::str(iec_text(body)));
      }

      pou.set("pragmas", take_pending_pragmas());
      pous.push(pou);
      pou_name = "";
      return NULL;
    }

    void *print_step(symbol_c *symbol, symbol_c *step_name, symbol_c *action_association_list, bool initial) {
      std::string name = iec_text(step_name);
      json_value_c step    = json_value_c::object();
      json_value_c actions = json_value_c::array();
      list_c *list = dynamic_cast<list_c *>(action_association_list);
      for (int i = 0; (NULL != list) && (i < list->n); i++) {
        action_association_c *assoc = dynamic_cast<action_association_c *>(list->get_element(i));
        if (NULL == assoc) continue;
        json_value_c a = json_value_c::object();
        a.set("name", json_value_c::str(iec_text(assoc->action_name)));
        action_qualifier_c *q = dynamic_cast<action_qualifier_c *>(assoc->action_qualifier);
        a.set("qualifier", json_value_c::str((NULL == q)? "N" : iec_text(q->action_qualifier)));
        if ((NULL != q) && (NULL != q->action_time)) {
          a.set("time", json_value_c::str(iec_text(q->action_time)));
          std::string ms = duration_ms(q->action_time);
          if (!ms.empty()) a.set("time_ms", json_value_c::raw(ms));
        }
        if ((NULL != assoc->indicator_name_list) && (dynamic_cast<list_c *>(assoc->indicator_name_list)->n > 0)) {
          json_value_c ind = json_value_c::array();
          list_c *il = dynamic_cast<list_c *>(assoc->indicator_name_list);
          for (int j = 0; (NULL != il) && (j < il->n); j++) ind.push(json_value_c::str(iec_text(il->get_element(j))));
          a.set("indicators", ind);
        }
        actions.push(a);
      }
      step.set("name",    json_value_c::str(name));
      step.set("initial", json_value_c::boolean(initial));
      step.set("actions", actions);
      sfc_steps.push(step);
      map_src(pou_name + "/step/" + name, symbol);
      return NULL;
    }

    static json_value_c step_names(symbol_c *steps) {
      json_value_c res = json_value_c::array();
      steps_c *s = dynamic_cast<steps_c *>(steps);
      if (NULL == s) return res;
      if (NULL != s->step_name) res.push(json_value_c::str(iec_text(s->step_name)));
      list_c *list = dynamic_cast<list_c *>(s->step_name_list);
      for (int i = 0; (NULL != list) && (i < list->n); i++) res.push(json_value_c::str(iec_text(list->get_element(i))));
      return res;
    }


  public:
/***************************/
/* B 0 - Programming Model */
/***************************/
    void *visit(library_c *symbol) {
      for (int i = 0; i < symbol->n; i++) {
        symbol_c *element = symbol->get_element(i);
        if      (NULL != dynamic_cast<disable_code_generation_pragma_c *>(element)) code_generation_enabled = false;
        else if (NULL != dynamic_cast< enable_code_generation_pragma_c *>(element)) code_generation_enabled = true;
        else if (NULL != dynamic_cast<pragma_c *>(element)) {
          if (code_generation_enabled) pending_pragmas.push(json_value_c::str(pragma_text(dynamic_cast<pragma_c *>(element))));
        }
        else if (code_generation_enabled) element->accept(*this);
        else pending_pragmas = json_value_c::array();  /* POU of the standard library: not printed */
      }

      json_value_c root = json_value_c::object();
      root.set("pous", pous);
      root.set("configuration", (configurations.size() > 0)? configurations_first : json_value_c::raw("null"));
      if (configurations.size() > 1) root.set("configurations", configurations);  /* rare: more than one CONFIGURATION */
      root.set("source_map", source_map);

      std::ostringstream out;
      root.print(out);
      out << "\n";
      s4o.print(out.str());
      return NULL;
    }

    /* data types are not part of the model (yet) */
    void *visit(data_type_declaration_c *symbol) {pending_pragmas = json_value_c::array(); return NULL;}


/**************************************/
/* B.1.5 - Program organization units */
/**************************************/
    void *visit(function_declaration_c *symbol) {
      return print_pou(symbol, "function", symbol->derived_function_name, symbol->type_name, symbol->var_declarations_list, symbol->function_body);
    }
    void *visit(function_block_declaration_c *symbol) {
      return print_pou(symbol, "function_block", symbol->fblock_name, NULL, symbol->var_declarations, symbol->fblock_body);
    }
    void *visit(program_declaration_c *symbol) {
      return print_pou(symbol, "program", symbol->program_type_name, NULL, symbol->var_declarations, symbol->function_block_body);
    }


/***************************************/
/* B.1.4.3 - Declaration & Initialisation */
/***************************************/
    /* the variable blocks: set the class of the variables, and iterate */
    void *visit(input_declarations_c *symbol)              {return var_block("VAR_INPUT",    symbol->option, symbol->input_declaration_list);}
    void *visit(output_declarations_c *symbol)             {return var_block("VAR_OUTPUT",   symbol->option, symbol->var_init_decl_list);}
    void *visit(input_output_declarations_c *symbol)       {return var_block("VAR_IN_OUT",   NULL,           symbol->var_declaration_list);}
    void *visit(var_declarations_c *symbol)                {return var_block("VAR",          symbol->option, symbol->var_init_decl_list);}
    void *visit(retentive_var_declarations_c *symbol)      {var_block("VAR", NULL, NULL); var_option = "RETAIN";
                                                            if (NULL != symbol->var_init_decl_list) symbol->var_init_decl_list->accept(*this);
                                                            var_option = ""; return NULL;}
    void *visit(located_var_declarations_c *symbol)        {return var_block("VAR",          symbol->option, symbol->located_var_decl_list);}
    void *visit(incompl_located_var_declarations_c *symbol){return var_block("VAR",          symbol->option, symbol->incompl_located_var_decl_list);}
    void *visit(external_var_declarations_c *symbol)       {return var_block("VAR_EXTERNAL", symbol->option, symbol->external_declaration_list);}
    void *visit(global_var_declarations_c *symbol)         {return var_block("VAR_GLOBAL",   symbol->option, symbol->global_var_decl_list);}
    void *visit(function_var_decls_c *symbol)              {return var_block("VAR",          symbol->option, symbol->decl_list);}
    void *visit(temp_var_decls_c *symbol)                  {return var_block("VAR_TEMP",     NULL,           symbol->var_decl_list);}
    void *visit(non_retentive_var_decls_c *symbol)         {var_block("VAR", NULL, NULL); var_option = "NON_RETAIN";
                                                            if (NULL != symbol->var_decl_list) symbol->var_decl_list->accept(*this);
                                                            var_option = ""; return NULL;}

    /* the declarations themselves */
    void *visit(en_param_declaration_c *symbol) {
      if (typeid(*(symbol->method)) != typeid(explicit_definition_c)) return NULL;  // implicit EN
      symbol_c *type, *init;
      split_spec_init(symbol->type_decl, type, init);
      add_var(symbol->name, type, init, NULL, var_pragmas(symbol));
      return NULL;
    }
    void *visit(eno_param_declaration_c *symbol) {
      if (typeid(*(symbol->method)) != typeid(explicit_definition_c)) return NULL;  // implicit ENO
      add_var(symbol->name, symbol->type, NULL, NULL, var_pragmas(symbol));
      return NULL;
    }
    void *visit(edge_declaration_c *symbol) {
      add_var_list(symbol, symbol->var1_list, &get_datatype_info_c::bool_type_name, NULL, iec_text(symbol->edge));
      return NULL;
    }
    void *visit(var1_init_decl_c *symbol) {
      symbol_c *type, *init;
      split_spec_init(symbol->spec_init, type, init);
      add_var_list(symbol, symbol->var1_list, type, init);
      return NULL;
    }
    void *visit(array_var_init_decl_c *symbol) {
      symbol_c *type, *init;
      split_spec_init(symbol->array_spec_init, type, init);
      add_var_list(symbol, symbol->var1_list, type, init);
      return NULL;
    }
    void *visit(structured_var_init_decl_c *symbol) {
      symbol_c *type, *init;
      split_spec_init(symbol->initialized_structure, type, init);
      add_var_list(symbol, symbol->var1_list, type, init);
      return NULL;
    }
    void *visit(fb_name_decl_c *symbol) {
      symbol_c *type, *init;
      split_spec_init(symbol->fb_spec_init, type, init);
      add_var_list(symbol, symbol->fb_name_list, type, init);
      return NULL;
    }
    void *visit(array_var_declaration_c *symbol) {
      add_var_list(symbol, symbol->var1_list, symbol->array_specification, NULL);
      return NULL;
    }
    void *visit(structured_var_declaration_c *symbol) {
      add_var_list(symbol, symbol->var1_list, symbol->structure_type_name, NULL);
      return NULL;
    }
    void *visit(single_byte_string_var_declaration_c *symbol) {
      symbol_c *type, *init;
      split_spec_init(symbol->single_byte_string_spec, type, init);
      add_var_list(symbol, symbol->var1_list, type, init);
      return NULL;
    }
    void *visit(double_byte_string_var_declaration_c *symbol) {
      symbol_c *type, *init;
      split_spec_init(symbol->double_byte_string_spec, type, init);
      add_var_list(symbol, symbol->var1_list, type, init);
      return NULL;
    }
    void *visit(located_var_decl_c *symbol) {
      symbol_c *type, *init;
      split_spec_init(symbol->located_var_spec_init, type, init);
      location_c *location = dynamic_cast<location_c *>(symbol->location);
      /* an anonymous located variable (AT %IX0.0 : BOOL) is named after its location */
      symbol_c *name = (NULL != symbol->variable_name)? symbol->variable_name : (symbol_c *)symbol->location;
      add_var(name, type, init, (NULL != location)? location->direct_variable : symbol->location, var_pragmas(symbol));
      return NULL;
    }
    void *visit(incompl_located_var_decl_c *symbol) {
      add_var(symbol->variable_name, symbol->var_spec, NULL, symbol->incompl_location, var_pragmas(symbol));
      return NULL;
    }
    void *visit(external_declaration_c *symbol) {
      add_var(symbol->global_var_name, symbol->specification, NULL, NULL, var_pragmas(symbol));
      return NULL;
    }
    void *visit(global_var_decl_c *symbol) {
      symbol_c *type, *init;
      split_spec_init(symbol->type_specification, type, init);
      global_var_spec_c *spec = dynamic_cast<global_var_spec_c *>(symbol->global_var_spec);
      if (NULL == spec) {  /* global_var_list */
        add_var_list(symbol, symbol->global_var_spec, type, init);
        return NULL;
      }
      location_c *location = dynamic_cast<location_c *>(spec->location);
      symbol_c *name = (NULL != spec->global_var_name)? spec->global_var_name : spec->location;
      add_var(name, type, init, (NULL != location)? location->direct_variable : spec->location, var_pragmas(symbol));
      return NULL;
    }


/*********************************************/
/* B.1.6  Sequential function chart elements */
/*********************************************/
    /* sequential_function_chart_c and sfc_network_c are iterated over by iterator_visitor_c */
    void *visit(initial_step_c *symbol) {return print_step(symbol, symbol->step_name, symbol->action_association_list, true);}
    void *visit(step_c *symbol)         {return print_step(symbol, symbol->step_name, symbol->action_association_list, false);}

    void *visit(transition_c *symbol) {
      json_value_c t = json_value_c::object();
      std::string key = (NULL != symbol->transition_name)? iec_text(symbol->transition_name) : "";
      if (!key.empty()) t.set("name", json_value_c::str(key));
      t.set("from", step_names(symbol->from_steps));
      t.set("to",   step_names(symbol->to_steps));
      transition_condition_c *cond = dynamic_cast<transition_condition_c *>(symbol->transition_condition);
      if (NULL != cond) {
        if (NULL != cond->transition_condition_st) t.set("condition_st", json_value_c::str(iec_text(cond->transition_condition_st)));
        if (NULL != cond->transition_condition_il) t.set("condition_il", json_value_c::str(iec_text(cond->transition_condition_il)));
      }
      if (NULL != symbol->integer) t.set("priority", json_integer(symbol->integer));
      sfc_transitions.push(t);
      std::ostringstream idx;
      idx << transition_count++;
      map_src(pou_name + "/transition/" + (key.empty()? idx.str() : key), symbol);
      return NULL;
    }

    void *visit(action_c *symbol) {
      std::string name = iec_text(symbol->action_name);
      json_value_c a = json_value_c::object();
      a.set("name", json_value_c::str(name));
      if (NULL != dynamic_cast<instruction_list_c *>(symbol->function_block_body))
        a.set("body_il", json_value_c::str(iec_text(symbol->function_block_body)));
      else
        a.set("body_st", json_value_c::str(iec_text(symbol->function_block_body)));
      sfc_actions.push(a);
      map_src(pou_name + "/action/" + name, symbol);
      return NULL;
    }


/********************************/
/* B 1.7 Configuration elements */
/********************************/
  private:
    json_value_c configurations_first;

    json_value_c print_resource(symbol_c *symbol, std::string name, symbol_c *type, symbol_c *globals, symbol_c *resource_declaration) {
      json_value_c res = json_value_c::object();
      json_value_c res_globals = json_value_c::array();
      res_tasks     = json_value_c::array();
      res_instances = json_value_c::array();
      std::string saved_pou_name = pou_name;

      pou_name = saved_pou_name + "/" + name;
      res.set("name", json_value_c::str(name));
      if (NULL != type) res.set("type", json_value_c::str(iec_text(type)));
      vars = &res_globals;
      if (NULL != globals) globals->accept(*this);
      vars = NULL;
      if (NULL != resource_declaration) resource_declaration->accept(*this);
      res.set("globals",   res_globals);
      res.set("tasks",     res_tasks);
      res.set("instances", res_instances);
      if (NULL != symbol) map_src(pou_name, symbol);
      pou_name = saved_pou_name;
      return res;
    }

  public:
    void *visit(configuration_declaration_c *symbol) {
      json_value_c conf        = json_value_c::object();
      json_value_c conf_globals = json_value_c::array();
      json_value_c resources   = json_value_c::array();

      begin_pou(symbol, symbol->configuration_name);
      conf.set("name", json_value_c::str(pou_name));
      vars = &conf_globals;
      if (NULL != symbol->global_var_declarations) symbol->global_var_declarations->accept(*this);
      vars = NULL;
      conf.set("globals", conf_globals);

      list_c *res_list = dynamic_cast<resource_declaration_list_c *>(symbol->resource_declarations);
      if (NULL != res_list) {
        for (int i = 0; i < res_list->n; i++) {
          resource_declaration_c *r = dynamic_cast<resource_declaration_c *>(res_list->get_element(i));
          if (NULL != r) resources.push(print_resource(r, iec_text(r->resource_name), r->resource_type_name, r->global_var_declarations, r->resource_declaration));
        }
      } else {
        /* single_resource_declaration: an anonymous resource */
        resources.push(print_resource(NULL, "", NULL, NULL, symbol->resource_declarations));
      }
      conf.set("resources", resources);
      conf.set("pragmas", take_pending_pragmas());

      if (configurations.size() == 0) configurations_first = conf;
      configurations.push(conf);
      pou_name = "";
      return NULL;
    }

    void *visit(task_configuration_c *symbol) {
      json_value_c t = json_value_c::object();
      std::string name = iec_text(symbol->task_name);
      t.set("name", json_value_c::str(name));
      task_initialization_c *init = dynamic_cast<task_initialization_c *>(symbol->task_initialization);
      if (NULL != init) {
        if (NULL != init->single_data_source)   t.set("single",   json_value_c::str(iec_text(init->single_data_source)));
        if (NULL != init->interval_data_source) {
          t.set("interval", json_value_c::str(iec_text(init->interval_data_source)));
          std::string ms = duration_ms(init->interval_data_source);
          if (!ms.empty()) t.set("interval_ms", json_value_c::raw(ms));
        }
        if (NULL != init->priority_data_source) t.set("priority", json_integer(init->priority_data_source));
      }
      res_tasks.push(t);
      map_src(pou_name + "/task/" + name, symbol);
      return NULL;
    }

    void *visit(program_configuration_c *symbol) {
      json_value_c p = json_value_c::object();
      std::string name = iec_text(symbol->program_name);
      p.set("name", json_value_c::str(name));
      p.set("type", json_value_c::str(iec_text(symbol->program_type_name)));
      p.set("task", (NULL != symbol->task_name)? json_value_c::str(iec_text(symbol->task_name)) : json_value_c::raw("null"));
      if (NULL != symbol->retain_option) p.set("option", json_value_c::str(iec_text(symbol->retain_option)));
      res_instances.push(p);
      map_src(pou_name + "/instance/" + name, symbol);
      return NULL;
    }

}; /* class generate_json_c */



/***********************************************************************/
/***********************************************************************/

visitor_c *new_code_generator(stage4out_c *s4o, const char *builddir)  {return new generate_json_c(s4o);}
void delete_code_generator(visitor_c *code_generator) {delete code_generator;}
