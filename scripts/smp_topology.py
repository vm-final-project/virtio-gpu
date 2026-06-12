"""Reference model for Unikraft pthread affinity placement."""
from __future__ import annotations


def effective_mask(requested: int, online: int) -> int:
    effective = requested & online
    if effective == 0:
        raise ValueError("affinity has no online CPU")
    return effective


def choose_target(effective: int, current: int) -> int:
    if effective == 0:
        raise ValueError("affinity has no online CPU")
    if effective & (1 << current):
        return current
    return (effective & -effective).bit_length() - 1


def worker_masks(n_workers: int, n_lcpus: int) -> list[int]:
    if n_workers < 1 or n_workers > n_lcpus:
        raise ValueError("workers must fit available LCPUs")
    return [1 << i for i in range(n_workers)]
