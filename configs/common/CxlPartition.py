# Copyright (c) 2026
# All rights reserved.

from collections import namedtuple
from fractions import Fraction


CXL_HOST_WINDOW_ALIGN = 256 * 1024 * 1024

CxlHostPartition = namedtuple("CxlHostPartition", ["offset", "size"])


def _largest_remainder_units(total_units, weights):
    weights = [Fraction(str(w)) for w in weights]
    total_weight = sum(weights)
    if total_weight <= 0:
        raise ValueError("--host-cxl-ratios sum must be > 0")

    scaled = [w * total_units / total_weight for w in weights]
    units = [int(v) for v in scaled]
    remaining = total_units - sum(units)
    frac_order = sorted(
        range(len(weights)),
        key=lambda i: (scaled[i] - units[i], weights[i], -i),
        reverse=True,
    )
    for i in range(remaining):
        units[frac_order[i]] += 1
    return units


def _ensure_nonzero_units(units):
    units = list(units)
    if len(units) <= 1:
        return units

    for idx, val in enumerate(units):
        if val != 0:
            continue
        donor = max(range(len(units)), key=lambda i: units[i])
        if units[donor] <= 1:
            raise ValueError(
                "CXL memory pool is too small to give every host a "
                "non-zero 256MiB window"
            )
        units[donor] -= 1
        units[idx] = 1

    return units


def partition_cxl_memory(total_size, weights, align=CXL_HOST_WINDOW_ALIGN):
    if total_size <= 0:
        raise ValueError("--cxl-mem-size must be > 0")
    if align <= 0:
        raise ValueError("CXL host window alignment must be > 0")
    if total_size % align != 0:
        raise ValueError("--cxl-mem-size must be a multiple of 256MiB")
    if not weights:
        raise ValueError("--host-cxl-ratios must contain at least one value")
    if any(w < 0 for w in weights):
        raise ValueError("--host-cxl-ratios values must be >= 0")

    total_units = total_size // align
    if total_units < len(weights):
        raise ValueError(
            "CXL memory pool is too small to allocate one 256MiB "
            "window per host"
        )

    units = [1] * len(weights)
    remaining_units = total_units - len(weights)
    if remaining_units > 0:
        extra_units = _largest_remainder_units(remaining_units, weights)
        units = [units[i] + extra_units[i] for i in range(len(units))]
    units = _ensure_nonzero_units(units)

    parts = []
    offset = 0
    for unit_count in units:
        size = unit_count * align
        parts.append(CxlHostPartition(offset=offset, size=size))
        offset += size

    return parts
