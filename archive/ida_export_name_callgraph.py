# -*- coding: utf-8 -*-
"""
按入口名称导出向下可达的调用图。
- <name>.report.txt: 人看用概览
- <name>.functions.txt: 便于 diff/compare 的函数清单
- <name>.edges.txt: 便于 diff/compare 的边清单
- compare_*.txt: 多入口时自动输出交集/差集
- unresolved 会列出 IDA 当前无法解析目标的间接调用点
"""

import collections
import itertools
import os
import re

import ida_auto
import ida_bytes
import ida_funcs
import ida_idaapi
import idautils
import idc

ENTRY_NAMES = [
    "DSUIMusicMenu_HandlePlayNextMusic",
    "DSUIConstructionCustomizeMenuFunction_OnReleaseAcceptListItem",
]

OUTPUT_DIR = ""
COMPARE_BY_NAME_ONLY = False
BADADDR = ida_idaapi.BADADDR
def sanitize_filename(text):
    return re.sub(r'[<>:"/\\|?*]+', "_", text).strip(" .")
def ensure_output_dir():
    if OUTPUT_DIR:
        out_dir = OUTPUT_DIR
    else:
        idb_dir = os.path.dirname(idc.get_idb_path())
        out_dir = os.path.join(idb_dir, "xref_exports")
    os.makedirs(out_dir, exist_ok=True)
    return out_dir
def get_func_name(func_ea):
    name = idc.get_func_name(func_ea)
    if name:
        return name
    name = idc.get_name(func_ea, idc.GN_VISIBLE)
    return name if name else "sub_%X" % func_ea
def fmt_ea(ea):
    return "0x%X" % ea
def resolve_entry(name):
    ea = idc.get_name_ea_simple(name)
    if ea == BADADDR:
        raise RuntimeError("找不到名字: %s" % name)
    func = ida_funcs.get_func(ea)
    if not func:
        raise RuntimeError("名字存在，但不在函数内: %s (%s)" % (name, fmt_ea(ea)))
    return func.start_ea
def iter_direct_callees(func_ea):
    func = ida_funcs.get_func(func_ea)
    if not func:
        return

    seen_edges = set()
    seen_unresolved = set()

    for insn_ea in idautils.FuncItems(func.start_ea):
        flags = ida_bytes.get_full_flags(insn_ea)
        if not ida_bytes.is_code(flags):
            continue

        mnem = idc.print_insn_mnem(insn_ea).lower()
        refs = set()
        for ref in idautils.CodeRefsFrom(insn_ea, 0):
            target_func = ida_funcs.get_func(ref)
            if not target_func:
                continue
            callee_ea = target_func.start_ea
            if callee_ea == func.start_ea:
                continue
            refs.add(callee_ea)

        if refs:
            for callee_ea in sorted(refs):
                edge = (func.start_ea, callee_ea)
                if edge in seen_edges:
                    continue
                seen_edges.add(edge)
                yield ("edge", func.start_ea, insn_ea, callee_ea)
            continue

        if mnem.startswith("call"):
            operand = idc.print_operand(insn_ea, 0)
            unresolved = (func.start_ea, insn_ea, operand)
            if unresolved in seen_unresolved:
                continue
            seen_unresolved.add(unresolved)
            yield ("unresolved", func.start_ea, insn_ea, operand)
def collect_callgraph(entry_name):
    entry_ea = resolve_entry(entry_name)
    queue = collections.deque([(entry_ea, 0)])
    visited = set()
    min_depth = {entry_ea: 0}
    parents = collections.defaultdict(set)
    edges = set()
    unresolved = []

    while queue:
        func_ea, depth = queue.popleft()
        if func_ea in visited:
            continue
        visited.add(func_ea)

        for item in iter_direct_callees(func_ea):
            kind = item[0]
            if kind == "unresolved":
                _, caller_ea, insn_ea, operand = item
                unresolved.append((caller_ea, insn_ea, operand))
                continue

            _, caller_ea, _, callee_ea = item
            edges.add((caller_ea, callee_ea))
            parents[callee_ea].add(caller_ea)

            next_depth = depth + 1
            old_depth = min_depth.get(callee_ea)
            if old_depth is None or next_depth < old_depth:
                min_depth[callee_ea] = next_depth
            if callee_ea not in visited:
                queue.append((callee_ea, next_depth))

    functions = sorted(min_depth.keys(), key=lambda ea: (get_func_name(ea).lower(), ea))
    max_depth = max(min_depth.values()) if min_depth else 0
    return {
        "entry_name": entry_name,
        "entry_ea": entry_ea,
        "functions": functions,
        "edges": sorted(edges, key=lambda p: (get_func_name(p[0]).lower(), p[0], get_func_name(p[1]).lower(), p[1])),
        "min_depth": min_depth,
        "parents": parents,
        "unresolved": sorted(unresolved, key=lambda x: (get_func_name(x[0]).lower(), x[0], x[1])),
        "max_depth": max_depth,
    }
def write_lines(path, lines):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for line in lines:
            f.write(line)
            f.write("\n")
def export_single(result, out_dir):
    safe_name = sanitize_filename(result["entry_name"])
    report_path = os.path.join(out_dir, "%s.report.txt" % safe_name)
    funcs_path = os.path.join(out_dir, "%s.functions.txt" % safe_name)
    edges_path = os.path.join(out_dir, "%s.edges.txt" % safe_name)

    report_lines = [
        "ENTRY_NAME\t%s" % result["entry_name"],
        "ENTRY_EA\t%s" % fmt_ea(result["entry_ea"]),
        "TOTAL_FUNCTIONS\t%d" % len(result["functions"]),
        "TOTAL_EDGES\t%d" % len(result["edges"]),
        "MAX_DEPTH\t%d" % result["max_depth"],
        "UNRESOLVED_CALLS\t%d" % len(result["unresolved"]),
        "",
        "[FUNCTIONS_BY_NAME]",
        "name\tea\tmin_depth",
    ]
    for func_ea in result["functions"]:
        report_lines.append(
            "%s\t%s\t%d" % (
                get_func_name(func_ea),
                fmt_ea(func_ea),
                result["min_depth"][func_ea],
            )
        )

    report_lines.extend(["", "[EDGES]", "caller_name\tcaller_ea\tcallee_name\tcallee_ea"])
    for caller_ea, callee_ea in result["edges"]:
        report_lines.append(
            "%s\t%s\t%s\t%s" % (
                get_func_name(caller_ea),
                fmt_ea(caller_ea),
                get_func_name(callee_ea),
                fmt_ea(callee_ea),
            )
        )

    report_lines.extend(["", "[UNRESOLVED_CALLS]", "caller_name\tinsn_ea\toperand"])
    for caller_ea, insn_ea, operand in result["unresolved"]:
        report_lines.append(
            "%s\t%s\t%s" % (
                get_func_name(caller_ea),
                fmt_ea(insn_ea),
                operand,
            )
        )

    func_lines = []
    for func_ea in result["functions"]:
        func_lines.append(
            "%s\t%s\tdepth=%d" % (
                get_func_name(func_ea),
                fmt_ea(func_ea),
                result["min_depth"][func_ea],
            )
        )

    edge_lines = []
    for caller_ea, callee_ea in result["edges"]:
        edge_lines.append(
            "%s\t%s\t->\t%s\t%s" % (
                get_func_name(caller_ea),
                fmt_ea(caller_ea),
                get_func_name(callee_ea),
                fmt_ea(callee_ea),
            )
        )

    write_lines(report_path, report_lines)
    write_lines(funcs_path, func_lines)
    write_lines(edges_path, edge_lines)
    return report_path, funcs_path, edges_path
def make_compare_key(func_ea):
    if COMPARE_BY_NAME_ONLY:
        return get_func_name(func_ea)
    return fmt_ea(func_ea)
def collect_common_ancestors(func_ea, result, ea_to_key, common_keys):
    stack = list(result["parents"].get(func_ea, ()))
    seen = set()
    ancestors = set()

    while stack:
        parent_ea = stack.pop()
        if parent_ea in seen:
            continue
        seen.add(parent_ea)
        if parent_ea == func_ea:
            stack.extend(result["parents"].get(parent_ea, ()))
            continue
        parent_key = ea_to_key.get(parent_ea)
        if parent_key in common_keys:
            ancestors.add(parent_key)
        stack.extend(result["parents"].get(parent_ea, ()))
    return ancestors
def compare_results(result_a, result_b, out_dir):
    name_a, name_b = result_a["entry_name"], result_b["entry_name"]
    safe_pair = "%s__%s" % (sanitize_filename(name_a), sanitize_filename(name_b))
    out_path = os.path.join(out_dir, "compare_%s.txt" % safe_pair)

    map_a = {make_compare_key(ea): ea for ea in result_a["functions"]}
    map_b = {make_compare_key(ea): ea for ea in result_b["functions"]}
    common_keys = sorted(set(map_a) & set(map_b))
    only_a_keys = sorted(set(map_a) - set(map_b))
    only_b_keys = sorted(set(map_b) - set(map_a))
    ea_to_key_a = {ea: key for key, ea in map_a.items()}
    ea_to_key_b = {ea: key for key, ea in map_b.items()}
    shared_ancestor_map = {}
    collapsed_common, pruned_common = [], []

    for key in common_keys:
        ancestors_a = collect_common_ancestors(map_a[key], result_a, ea_to_key_a, set(common_keys))
        ancestors_b = collect_common_ancestors(map_b[key], result_b, ea_to_key_b, set(common_keys))
        shared_ancestors = ancestors_a & ancestors_b
        shared_ancestor_map[key] = shared_ancestors
        if not shared_ancestors:
            collapsed_common.append(key)
    collapsed_set = set(collapsed_common)

    for key in common_keys:
        if key in collapsed_set:
            continue
        covered_by = sorted(shared_ancestor_map[key] & collapsed_set)
        if not covered_by:
            covered_by = sorted(
                ancestor_key for ancestor_key in collapsed_common
                if ancestor_key in shared_ancestor_map[key]
            )
        pruned_common.append((key, covered_by))

    lines = [
        "COMPARE_A\t%s" % name_a,
        "COMPARE_B\t%s" % name_b,
        "KEY_MODE\t%s" % ("name" if COMPARE_BY_NAME_ONLY else "address"),
        "COMMON_COLLAPSED_COUNT\t%d" % len(collapsed_common),
        "COMMON_RAW_COUNT\t%d" % len(common_keys),
        "ONLY_A_COUNT\t%d" % len(only_a_keys),
        "ONLY_B_COUNT\t%d" % len(only_b_keys),
        "",
        "[COMMON_FUNCTIONS_COLLAPSED]",
        "name\tea",
    ]

    for key in collapsed_common:
        ea = map_a[key]
        lines.append("%s\t%s" % (get_func_name(ea), fmt_ea(ea)))

    lines.extend(["", "[COMMON_FUNCTIONS_PRUNED]", "name\tea\tcovered_by_common_ancestors"])
    for key, ancestor_keys in pruned_common:
        ea = map_a[key]
        ancestor_names = ", ".join(get_func_name(map_a[ancestor_key]) for ancestor_key in ancestor_keys)
        lines.append("%s\t%s\t%s" % (get_func_name(ea), fmt_ea(ea), ancestor_names))

    lines.extend(["", "[COMMON_FUNCTIONS_RAW]", "name\tea"])
    for key in common_keys:
        ea = map_a[key]
        lines.append("%s\t%s" % (get_func_name(ea), fmt_ea(ea)))

    lines.extend(["", "[ONLY_A]", "name\tea"])
    for key in only_a_keys:
        ea = map_a[key]
        lines.append("%s\t%s" % (get_func_name(ea), fmt_ea(ea)))

    lines.extend(["", "[ONLY_B]", "name\tea"])
    for key in only_b_keys:
        ea = map_b[key]
        lines.append("%s\t%s" % (get_func_name(ea), fmt_ea(ea)))

    write_lines(out_path, lines)
    return out_path
def main():
    ida_auto.auto_wait()
    out_dir = ensure_output_dir()
    results = []

    for entry_name in ENTRY_NAMES:
        result = collect_callgraph(entry_name)
        report_path, funcs_path, edges_path = export_single(result, out_dir)
        results.append(result)
        print("[ok] %s" % entry_name)
        print("  report   : %s" % report_path)
        print("  functions: %s" % funcs_path)
        print("  edges    : %s" % edges_path)

    if len(results) >= 2:
        for result_a, result_b in itertools.combinations(results, 2):
            compare_path = compare_results(result_a, result_b, out_dir)
            print("[ok] compare : %s" % compare_path)
if __name__ == "__main__":
    main()
