#!/usr/bin/env python3
import sys
import json

# ---------------------------
# Helpers for names / brackets
# ---------------------------

def split_optional_name(raw):
    """
    For answer.json only.

    Input: raw string like " b ", "[*pp]", "[ **pp ]"
    Output:
      ("b", False)        # required
      ("*pp", True)       # optional
    """
    if not isinstance(raw, str):
        return "", False
    s = raw.strip()
    if s.startswith("[") and s.endswith("]"):
        inner = s[1:-1].strip()
        return inner, True
    return s, False


def normalize_student_name(raw):
    """
    For student (testcase.json) names.

    Just strip whitespace; no optional/bracket semantics here.
    """
    if not isinstance(raw, str):
        return ""
    return raw.strip()


# ---------------------------
# TREF / TGEN
# ---------------------------

def parse_answer_name_list(lst):
    """
    For answer.json TREF/TGEN.

    Input:  [" b ", "[*pp]", "c"]
    Output: (required_set, optional_set)
            ({"b", "c"}, {"*pp"})
    """
    required = set()
    optional = set()
    for raw in lst or []:
        name, is_opt = split_optional_name(raw)
        if not name:
            continue
        if is_opt:
            optional.add(name)
        else:
            required.add(name)
    return required, optional


def parse_student_name_list(lst):
    """
    For testcase.json TREF/TGEN.

    Input:  ["b", "*pp", " c "]
    Output: {"b", "*pp", "c"}
    """
    result = set()
    for raw in lst or []:
        name = normalize_student_name(raw)
        if name:
            result.add(name)
    return result


def compare_name_field(stmt, field, ans_list, stu_list, messages):
    ans_req, ans_opt = parse_answer_name_list(ans_list)
    stu_set = parse_student_name_list(stu_list)

    missing_required = ans_req - stu_set
    allowed = ans_req | ans_opt
    extra = stu_set - allowed

    if missing_required or extra:
        messages.append(f"[{stmt}] {field} mismatch")
        if missing_required:
            messages.append(f"  missing required: {sorted(missing_required)}")
        if extra:
            messages.append(f"  extra (not required/optional): {sorted(extra)}")
        return False
    return True


# ---------------------------
# TDEF
# ---------------------------

def parse_answer_tdef(d):
    """
    For answer.json TDEF.

    Input: {"p": 1, "[*pp]": 3}
    Output:
      required = {"p": 1}
      optional = {"*pp": 3}
    """
    required = {}
    optional = {}
    for raw_k, v in (d or {}).items():
        name, is_opt = split_optional_name(raw_k)
        if not name:
            continue
        stmt_id = int(v)
        if is_opt:
            optional[name] = stmt_id
        else:
            required[name] = stmt_id
    return required, optional


def parse_student_tdef(d):
    """
    For testcase.json TDEF.

    Just strip name and cast stmt id to int.
    """
    result = {}
    for raw_k, v in (d or {}).items():
        name = normalize_student_name(raw_k)
        if not name:
            continue
        result[name] = int(v)
    return result


def compare_tdef(stmt, ans_dict, stu_dict, messages):
    ans_req, ans_opt = parse_answer_tdef(ans_dict)
    stu = parse_student_tdef(stu_dict)

    ok = True

    # Missing / mismatched required
    for name, ans_stmt in ans_req.items():
        if name not in stu:
            messages.append(f"[{stmt}] TDEF missing required entry for '{name}' (expected stmt {ans_stmt})")
            ok = False
        elif stu[name] != ans_stmt:
            messages.append(
                f"[{stmt}] TDEF stmt mismatch for required '{name}': expected {ans_stmt}, got {stu[name]}"
            )
            ok = False

    # Optional: if present, must match
    for name, ans_stmt in ans_opt.items():
        if name in stu and stu[name] != ans_stmt:
            messages.append(
                f"[{stmt}] TDEF stmt mismatch for optional '{name}': expected {ans_stmt}, got {stu[name]}"
            )
            ok = False

    # Extra entries in student
    allowed_names = set(ans_req.keys()) | set(ans_opt.keys())
    extra = set(stu.keys()) - allowed_names
    if extra:
        messages.append(f"[{stmt}] TDEF has extra names not in answer: {sorted(extra)}")
        ok = False

    return ok


# ---------------------------
# DEP
# ---------------------------

def parse_answer_dep_list(lst):
    """
    For answer.json DEP.

    Input example:
      [
        {"var": "pp",    "src_stmt": 2, "dst_stmt": 3, "type": "flow"},
        {"var": "[*pp]", "src_stmt": 2, "dst_stmt": 4, "type": "flow"}
      ]

    Output:
      required = {("pp", 2, 3, "flow")}
      optional = {("*pp", 2, 4, "flow")}
    """
    required = set()
    optional = set()
    for dep in lst or []:
        raw_var = dep.get("var")
        var, is_opt = split_optional_name(raw_var)
        if not var:
            continue
        src = int(dep.get("src_stmt"))
        dst = int(dep.get("dst_stmt"))
        dep_type = dep.get("type")
        key = (var, src, dst, dep_type)
        if is_opt:
            optional.add(key)
        else:
            required.add(key)
    return required, optional


def parse_student_dep_list(lst):
    """
    For testcase.json DEP.

    No optional semantics here; just normalized tuples.
    """
    result = set()
    for dep in lst or []:
        raw_var = dep.get("var")
        var = normalize_student_name(raw_var)
        if not var:
            continue
        src = int(dep.get("src_stmt"))
        dst = int(dep.get("dst_stmt"))
        dep_type = dep.get("type")
        result.add((var, src, dst, dep_type))
    return result


def compare_dep(stmt, ans_list, stu_list, messages):
    ans_req, ans_opt = parse_answer_dep_list(ans_list)
    stu_set = parse_student_dep_list(stu_list)

    missing_required = ans_req - stu_set
    allowed = ans_req | ans_opt
    extra = stu_set - allowed

    ok = True
    if missing_required or extra:
        messages.append(f"[{stmt}] DEP mismatch")
        if missing_required:
            messages.append(f"  missing required: {sorted(list(missing_required))}")
        if extra:
            messages.append(f"  extra (not required/optional): {sorted(list(extra))}")
        ok = False
    return ok


# ---------------------------
# TEQUIV
# ---------------------------

def parse_answer_tequiv(lst):
    """
    For answer.json TEQUIV.

    Input example:
      [["*p", "y"], ["[**pp]", "y"]]

    Treat pairs as unordered:
      pair ("*p","y") == ("y","*p")

    Output:
      required = { frozenset({"*p","y"}) }
      optional = { frozenset({"**pp","y"}) }
    """
    required = set()
    optional = set()
    for pair in lst or []:
        if not isinstance(pair, (list, tuple)) or len(pair) != 2:
            continue
        raw_a, raw_b = pair
        a, opt_a = split_optional_name(raw_a)
        b, opt_b = split_optional_name(raw_b)
        if not a or not b:
            continue
        is_opt = opt_a or opt_b
        key = frozenset({a, b})
        if is_opt:
            optional.add(key)
        else:
            required.add(key)
    return required, optional


def parse_student_tequiv(lst):
    """
    For testcase.json TEQUIV.

    Just strip names and build unordered pairs.
    """
    result = set()
    for pair in lst or []:
        if not isinstance(pair, (list, tuple)) or len(pair) != 2:
            continue
        a = normalize_student_name(pair[0])
        b = normalize_student_name(pair[1])
        if not a or not b:
            continue
        result.add(frozenset({a, b}))
    return result


def compare_tequiv(stmt, ans_list, stu_list, messages):
    ans_req, ans_opt = parse_answer_tequiv(ans_list)
    stu_set = parse_student_tequiv(stu_list)

    missing_required = ans_req - stu_set
    allowed = ans_req | ans_opt
    extra = stu_set - allowed

    ok = True
    if missing_required or extra:
        messages.append(f"[{stmt}] TEQUIV mismatch")
        if missing_required:
            # show each pair as sorted list for readability
            pretty_missing = [sorted(list(p)) for p in missing_required]
            messages.append(f"  missing required: {sorted(pretty_missing)}")
        if extra:
            pretty_extra = [sorted(list(p)) for p in extra]
            messages.append(f"  extra (not required/optional): {sorted(pretty_extra)}")
        ok = False
    return ok


# ---------------------------
# Main
# ---------------------------

def main():
    if len(sys.argv) != 3:
        print("Usage: python hw2_checker.py answer.json testcase.json")
        sys.exit(1)

    answer_path = sys.argv[1]
    student_path = sys.argv[2]

    try:
        with open(answer_path, "r") as f:
            ans = json.load(f)
    except Exception as e:
        print(f"Error reading answer file '{answer_path}': {e}")
        sys.exit(1)

    try:
        with open(student_path, "r") as f:
            stu = json.load(f)
    except Exception as e:
        print(f"Error reading student file '{student_path}': {e}")
        sys.exit(1)

    messages = []
    all_ok = True

    # Compare per statement S1, S2, ...
    ans_stmts = set(ans.keys())
    stu_stmts = set(stu.keys())

    missing_stmts = ans_stmts - stu_stmts
    extra_stmts = stu_stmts - ans_stmts

    if missing_stmts:
        all_ok = False
        messages.append("Missing statements in student output: " + ", ".join(sorted(missing_stmts)))
    if extra_stmts:
        # Usually harmless, but warn.
        messages.append("Extra statements in student output (ignored): " + ", ".join(sorted(extra_stmts)))

    for stmt in sorted(ans_stmts):
        if stmt not in stu:
            continue  # already reported missing
        ans_entry = ans[stmt]
        stu_entry = stu[stmt]

        # TREF
        if not compare_name_field(stmt, "TREF",
                                  ans_entry.get("TREF", []),
                                  stu_entry.get("TREF", []),
                                  messages):
            all_ok = False

        # TGEN
        if not compare_name_field(stmt, "TGEN",
                                  ans_entry.get("TGEN", []),
                                  stu_entry.get("TGEN", []),
                                  messages):
            all_ok = False

        # TDEF
        if not compare_tdef(stmt,
                            ans_entry.get("TDEF", {}),
                            stu_entry.get("TDEF", {}),
                            messages):
            all_ok = False

        # TEQUIV
        if not compare_tequiv(stmt,
                              ans_entry.get("TEQUIV", []),
                              stu_entry.get("TEQUIV", []),
                              messages):
            all_ok = False

        # DEP
        if not compare_dep(stmt,
                           ans_entry.get("DEP", []),
                           stu_entry.get("DEP", []),
                           messages):
            all_ok = False

    if all_ok:
        print("All statements match. ✅")
        sys.exit(0)
    else:
        print("Mismatch found between answer and student output:")
        print()
        for m in messages:
            print(m)
        sys.exit(1)

if __name__ == "__main__":
    main()


