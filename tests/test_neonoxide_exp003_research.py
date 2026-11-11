"""Research-only differential check of evolved NeonOxide pipelines on REAL TalkPipe.

Each pipeline uses the TalkPipe ChatterLang parser/compiler, registered pure
TalkPipe segments and source, and the independent TalkPipe Pipe API oracle.
No candidate can introduce arbitrary Python, plugins, I/O, or other capabilities.
This is a test, not a production sandbox or an admission authority.
"""

from __future__ import annotations

import random
from collections.abc import Iterable, Iterator

import pytest

from talkpipe import compile as chatterlang_compile
from talkpipe.chatterlang.registry import register_segment, register_source
from talkpipe.pipe.core import segment, source

# Three distinct programs that emerged from 30 offline evolutionary runs.
CANDIDATES = (
    "split_unicode | join_space | fold_item | fold_needle | filter_substring",
    "split_unicode | join_space | fold_needle | fold_item | filter_substring",
    "split_unicode | join_space | lower_needle | upper_needle | "
    "lower_item | upper_item | filter_substring",
)
ALLOWED = frozenset(
    {
        "split_unicode",
        "join_space",
        "fold_item",
        "fold_needle",
        "lower_item",
        "lower_needle",
        "upper_item",
        "upper_needle",
        "filter_substring",
    }
)
CURRENT: tuple[tuple[str, ...], str] = ((), "")


def admission(candidate: str) -> str:
    """Fail closed on raw candidate text BEFORE invoking TalkPipe's compiler."""
    if not isinstance(candidate, str) or candidate not in CANDIDATES:
        raise ValueError("Unadmitted candidate")
    stages = candidate.split(" | ")
    if len(stages) > 7 or any(stage not in ALLOWED for stage in stages):
        raise ValueError("Unregistered stage")
    return "INPUT FROM nox_exp003_fixture | " + " | ".join(
        "nox_exp003_" + stage for stage in stages
    )


def set_case(items: list[str], needle: str) -> None:
    global CURRENT
    if (
        not isinstance(items, list)
        or len(items) > 100
        or any(not isinstance(item, str) or len(item) > 4096 for item in items)
        or sum(map(len, items)) > 65536
        or not isinstance(needle, str)
        or not 1 <= len(needle) <= 128
    ):
        raise ValueError("Out of bounds")
    CURRENT = (tuple(items), needle)


@register_source("nox_exp003_fixture")
@source()
def nox_exp003_fixture() -> Iterator[dict[str, str | list[str]]]:
    items, needle = CURRENT
    for item in items:
        yield {"display": item, "payload": item, "needle": needle}


def stage_transform(
    stage: str, records: Iterable[dict[str, str | list[str]]]
) -> Iterator[dict[str, str | list[str]]]:
    for rec in records:
        display = rec["display"]
        payload = rec["payload"]
        needle = rec["needle"]
        if not isinstance(display, str) or not isinstance(needle, str):
            raise TypeError("Wrong record schema")
        if stage == "split_unicode":
            if not isinstance(payload, str):
                raise TypeError("Expected raw text")
            yield {**rec, "payload": payload.split()}
        elif stage == "join_space":
            if not isinstance(payload, list):
                raise TypeError("Expected tokens")
            new_display = " ".join(payload)
            yield {**rec, "display": new_display, "payload": new_display}
        else:
            if not isinstance(payload, str):
                raise TypeError("Expected text")
            if stage == "fold_item":
                yield {**rec, "payload": payload.casefold()}
            elif stage == "fold_needle":
                yield {**rec, "needle": needle.casefold()}
            elif stage == "lower_item":
                yield {**rec, "payload": payload.lower()}
            elif stage == "lower_needle":
                yield {**rec, "needle": needle.lower()}
            elif stage == "upper_item":
                yield {**rec, "payload": payload.upper()}
            elif stage == "upper_needle":
                yield {**rec, "needle": needle.upper()}
            elif stage == "filter_substring":
                if needle in payload:
                    yield rec
            else:
                raise AssertionError("Unexpected operation")


# Only reviewed, fixed pure operations are registered. No dynamic imports.
for _stage in sorted(ALLOWED):

    def _make_stage(name: str):
        def transform(records: Iterable[dict[str, str | list[str]]]):
            yield from stage_transform(name, records)

        transform.__name__ = "nox_exp003_" + name
        return segment()(transform)

    register_segment("nox_exp003_" + _stage)(_make_stage(_stage))


@segment()
def reference_normalize(items: Iterable[str]) -> Iterator[str]:
    for item in items:
        yield " ".join(item.split())


@segment()
def reference_filter(items: Iterable[str], keyword: str) -> Iterator[str]:
    term = keyword.casefold()
    for item in items:
        if term in item.casefold():
            yield item


def pipe_api_oracle(items: list[str], needle: str) -> list[str]:
    # This is the approved talkpipe-mcp keyword_filter implementation's logic.
    pipeline = reference_normalize() | reference_filter(keyword=needle)
    return list(pipeline.transform(iter(items)))


def confirmation_cases() -> list[tuple[list[str], str]]:
    # Independent EXP-002 post-selection stress set, 10 fixed + 600 seeded.
    cases = [
        (["ẞ", "ß", "SS", "ss", "s", "Straße", "STRAẞE"], "ß"),
        (["ẞ", "ß", "SS", "ss", "s", "Straße", "STRAẞE"], "ẞ"),
        (["STRAẞE", "STRASSE", "Straße", "strasse"], "ß"),
        (["straẞe", "strasse", "STRASSE", "Straße"], "straße"),
        (["ﬃ", "FFI", "ffi", "Fi"], "ﬃ"),
        (["ſ", "S", "s", "Ŝ"], "ſ"),
        (["İ", "i", "i\u0307", "I", "ı"], "İ"),
        (["Σ", "ς", "σ", "S"], "ς"),
        (["a\u2003b", "a b", "a\tb", "ab"], "a b"),
        ([" \u00a0A\u2002B\t", " A   B ", "A B"], "a b"),
    ]
    rng = random.Random(0xBADC0DED)
    needles = (
        "ẞ", "ß", "ss", "SS", "Straße", "ﬃ", "ffi", "ſ", "s",
        "İ", "i\u0307", "Σ", "ς", "σ", "A B", "a b", "💡", "test", "𐐀", "𐐨",
    )
    bases = (
        "ẞ", "ß", "SS", "ss", "STRAẞE", "Straße", "strasse",
        "FFI", "ﬃ", "ſ", "S", "İ", "i\u0307", "Σ", "ς", "σ",
        "alpha beta", "a b", "💡", "test", "𐐀", "𐐨", "unrelated", "",
    )
    spaces = (" ", "  ", "\t", "\n", "\u2002", "\u2003", "\u00a0", "\r\n")
    for _ in range(600):
        term = rng.choice(needles)
        vals = []
        for _ in range(rng.randint(1, 22)):
            sample = rng.choice(
                [
                    rng.choice(bases), term, term.upper(), term.lower(),
                    rng.choice(bases) + " " + term,
                    term + " " + rng.choice(bases),
                ]
            )
            ws = rng.choice(spaces)
            vals.append(
                rng.choice(spaces)
                + ws.join(sample.split(" "))
                + rng.choice(spaces)
            )
        cases.append((vals, term))
    return cases


@pytest.mark.parametrize("candidate", CANDIDATES)
def test_real_chatterlang_against_pipe_api_610_cases(candidate: str) -> None:
    compiled = chatterlang_compile(admission(candidate))
    cases = confirmation_cases()
    assert len(cases) == 610
    for index, (items, needle) in enumerate(cases):
        set_case(items, needle)
        actual = [item["display"] for item in compiled()]
        expected = pipe_api_oracle(items, needle)
        assert actual == expected, (
            f"Real TalkPipe mismatch in candidate={candidate!r} case={index}: "
            f"actual={actual!r} expected={expected!r}"
        )


def test_reject_unauthorized_candidates_before_compilation() -> None:
    for raw in (
        "INPUT FROM echo | exec",
        "split_unicode | join_space | exec",
        "split_unicode | join_space | readFile",
        "split_unicode | join_space | $SECRET",
        "split_unicode | join_space | @foo",
        "split_unicode | join_space; INPUT FROM echo",
        "split_unicode | join_space | lambda[code='1']",
        "split_unicode | join_space | nox_unknown",
    ):
        with pytest.raises(ValueError):
            admission(raw)


def test_reference_adapter_result_cap() -> None:
    from itertools import islice

    compiled = chatterlang_compile(admission(CANDIDATES[0]))
    items = [" PIPE " for _ in range(100)]
    set_case(items, "pipe")
    iterator = (record["display"] for record in compiled())
    first_51 = list(islice(iterator, 51))
    expected = pipe_api_oracle(items, "pipe")
    assert first_51[:50] == expected[:50]
    assert len(first_51) == 51
    # The external adapter's 50-result limit is a HOST contract, not a
    # security property provided by these pure TalkPipe stages.
