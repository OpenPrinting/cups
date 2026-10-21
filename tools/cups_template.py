# tools/cups_template.py
"""
CUPS Template Engine parser in Python.
Faithful implementation of cgi_copy from cgi-bin/template.c in CUPS.
Used for previewing and smoke testing CUPS web interface templates.
"""
import re
import html
import urllib.parse
from typing import Dict, List, Any, Union

class CupsTemplateEngine:
    def __init__(self, templates_dir: str, doc_dir: str, lang: str = "pt_BR"):
        self.templates_dir = templates_dir
        self.doc_dir = doc_dir
        self.lang = lang

    def render_template(self, tmpl_name: str, context: Dict[str, Any], lang: str = None) -> str:
        lang_to_use = lang or self.lang
        content = self._load_template_file(tmpl_name, lang_to_use)
        return self._evaluate(content, context, index=0)

    def _load_template_file(self, tmpl_name: str, lang: str) -> str:
        import os
        # 1. Try templates/<lang>/<tmpl_name>
        if lang:
            p = os.path.join(self.templates_dir, lang, tmpl_name)
            if os.path.isfile(p):
                with open(p, "r", encoding="utf-8", errors="replace") as f:
                    return f.read()
            # Try short lang e.g. pt from pt_BR
            short_lang = lang.split("_")[0]
            p = os.path.join(self.templates_dir, short_lang, tmpl_name)
            if os.path.isfile(p):
                with open(p, "r", encoding="utf-8", errors="replace") as f:
                    return f.read()

        # 2. Try templates/<tmpl_name>
        p = os.path.join(self.templates_dir, tmpl_name)
        if os.path.isfile(p):
            with open(p, "r", encoding="utf-8", errors="replace") as f:
                return f.read()

        raise FileNotFoundError(f"Template not found: {tmpl_name} (lang={lang})")

    def _get_var(self, name: str, context: Dict[str, Any], index: int = 0) -> str:
        # Check ENV:
        if name.startswith("ENV:"):
            env_name = name[4:]
            return str(context.get("ENV", {}).get(env_name, ""))
        
        # Check cookie $
        if name.startswith("$"):
            return str(context.get("COOKIES", {}).get(name[1:], ""))

        # Check # count
        if name.startswith("#"):
            arr_name = name[1:]
            val = context.get(arr_name, [])
            if isinstance(val, list):
                return str(len(val))
            return "1" if val else "0"

        # Check array indexed or current element
        val = context.get(name)
        if val is None:
            # Case-insensitive fallback as CUPS variables are often case-insensitive
            for k, v in context.items():
                if k.lower() == name.lower():
                    val = v
                    break

        if isinstance(val, list):
            if 0 <= index < len(val):
                return str(val[index])
            return ""
        
        return str(val) if val is not None else ""

    def _evaluate(self, text: str, context: Dict[str, Any], index: int = 0) -> str:
        # Recursive descent parser for CUPS curly brace syntax
        result = []
        i = 0
        n = len(text)

        while i < n:
            ch = text[i]
            if ch == '\\' and i + 1 < n and text[i+1] == '{':
                result.append('{')
                i += 2
                continue
            elif ch == '{':
                # Parse tag
                end_pos, tag_output = self._parse_tag(text, i, context, index)
                result.append(tag_output)
                i = end_pos
            else:
                result.append(ch)
                i += 1

        return "".join(result)

    def _parse_tag(self, text: str, start: int, context: Dict[str, Any], index: int) -> tuple[int, str]:
        # text[start] == '{'
        # Scan until matching '}' accounting for nested braces and conditionals
        i = start + 1
        n = len(text)

        # Check loop: {[array_name] body }
        if i < n and text[i] == '[':
            # find ']'
            bracket_close = text.find(']', i)
            if bracket_close != -1:
                arr_name = text[i+1:bracket_close].strip()
                # Find matching '}' for loop
                loop_body_start = bracket_close + 1
                loop_body_end = self._find_matching_brace(text, start)
                loop_body = text[loop_body_start:loop_body_end-1]
                
                # Expand loop
                items = context.get(arr_name, [])
                if not isinstance(items, list):
                    items = [items] if items else []
                out_loop = []
                for idx in range(len(items)):
                    out_loop.append(self._evaluate(loop_body, context, index=idx))
                return loop_body_end, "".join(out_loop)

        # Find matching '}' for this tag
        tag_end = self._find_matching_brace(text, start)
        tag_content = text[start+1:tag_end-1]

        # Check if tag is a conditional with '?'
        # Caution: question mark can be at start like {?VAR=val?...}
        start_search = 1 if tag_content.startswith('?') else 0
        q_pos = self._find_top_level_char(tag_content, '?', start_pos=start_search)
        if q_pos != -1:
            condition_expr = tag_content[:q_pos]
            branches = tag_content[q_pos+1:]
            
            # Find matching ':' between then and else branch
            colon_pos = self._find_top_level_char(branches, ':')
            if colon_pos != -1:
                then_branch = branches[:colon_pos]
                else_branch = branches[colon_pos+1:]
            else:
                then_branch = branches
                else_branch = ""

            # Evaluate condition
            cond_val = self._eval_condition(condition_expr, context, index)
            if cond_val:
                return tag_end, self._evaluate(then_branch, context, index)
            else:
                return tag_end, self._evaluate(else_branch, context, index)

        # Simple variable substitution
        var_name = tag_content.strip()
        if var_name.startswith("?"):
            val = self._get_var(var_name[1:], context, index)
            return tag_end, html.escape(val)
        elif var_name.startswith("%"):
            val = self._get_var(var_name[1:], context, index)
            return tag_end, urllib.parse.quote_plus(val)
        else:
            val = self._get_var(var_name, context, index)
            return tag_end, html.escape(val)

    def _find_matching_brace(self, text: str, open_pos: int) -> int:
        depth = 0
        i = open_pos
        n = len(text)
        while i < n:
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    return i + 1
            i += 1
        return n

    def _find_branch_colon(self, text: str) -> int:
        depth = 0
        in_tag = 0
        first_colon = -1
        for idx in range(len(text)):
            ch = text[idx]
            if ch == '{':
                depth += 1
            elif ch == '}':
                depth -= 1
            elif ch == '<':
                in_tag += 1
            elif ch == '>':
                if in_tag > 0:
                    in_tag -= 1
            elif ch == ':' and depth == 0 and in_tag == 0:
                if first_colon == -1:
                    first_colon = idx
                # Se for dois pontos precedido por quebra de linha (separador de bloco multilinha)
                prev_nl = text.rfind('\n', 0, idx)
                if prev_nl != -1 and text[prev_nl+1:idx].strip() == '':
                    return idx
        return first_colon

    def _find_top_level_char(self, text: str, target: str, start_pos: int = 0) -> int:
        depth = 0
        in_tag = 0
        for idx in range(start_pos, len(text)):
            ch = text[idx]
            if ch == '{':
                depth += 1
            elif ch == '}':
                depth -= 1
            elif ch == '<':
                in_tag += 1
            elif ch == '>':
                if in_tag > 0:
                    in_tag -= 1
            elif ch == target and depth == 0 and in_tag == 0:
                return idx
        return -1

    def _eval_condition(self, expr: str, context: Dict[str, Any], index: int) -> bool:
        # expr can be:
        # name (truthy check)
        # ?name (truthy check)
        # !name (falsy check)
        # name=val
        # ?name=val
        # name!=val
        # name<val
        # name>val
        # name~regex
        expr = expr.strip()
        if not expr:
            return False

        # Operators: !=, <=, >=, =, <, >, ~
        for op in ['!=', '<=', '>=', '=', '<', '>', '~']:
            pos = self._find_top_level_char(expr, op[0])
            if pos != -1 and expr[pos:pos+len(op)] == op:
                left_var = expr[:pos].strip()
                if left_var.startswith("?"):
                    left_var = left_var[1:]
                right_val = expr[pos+len(op):].strip()
                
                # evaluate right_val if it contains {var}
                if '{' in right_val:
                    right_val = self._evaluate(right_val, context, index)

                left_val = self._get_var(left_var, context, index)
                
                if op == '=':
                    return left_val == right_val
                elif op == '!=':
                    return left_val != right_val
                elif op == '~':
                    try:
                        return bool(re.search(right_val, left_val))
                    except:
                        return False
                elif op in ['<', '>', '<=', '>=']:
                    try:
                        num_left = float(left_val) if left_val else 0.0
                        num_right = float(right_val) if right_val else 0.0
                        if op == '<': return num_left < num_right
                        if op == '>': return num_left > num_right
                        if op == '<=': return num_left <= num_right
                        if op == '>=': return num_left >= num_right
                    except:
                        return False

        # Single var check
        if expr.startswith("!"):
            v = self._get_var(expr[1:], context, index)
            return not bool(v)
        if expr.startswith("?"):
            expr = expr[1:]
        v = self._get_var(expr, context, index)
        return bool(v)
