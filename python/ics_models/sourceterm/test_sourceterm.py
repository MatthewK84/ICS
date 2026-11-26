"""Basic test for the source-term model."""

from ics_models.sourceterm.model import source_term_table


def test_table_not_empty() -> None:
    table = source_term_table()
    assert len(table) > 0
    assert all(t.fragment_count > 0 for t in table)
