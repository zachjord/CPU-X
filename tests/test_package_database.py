# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('generator', ROOT / 'scripts/generate_package_database.py')
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)


class DatabaseTests(unittest.TestCase):
    def setUp(self):
        self.data = json.loads((ROOT / 'data/processors/packages.json').read_text())

    def test_checked_in_output(self):
        self.assertEqual(gen.generate(self.data), (ROOT / 'src/core/databases.h').read_text())

    def test_reproducible(self):
        self.assertEqual(gen.generate(self.data), gen.generate(copy.deepcopy(self.data)))

    def test_duplicates_and_conflicts(self):
        for socket in ('FL1', 'AM5'):
            data = copy.deepcopy(self.data)
            row = copy.deepcopy(data['records'][-1])
            row['socket'] = socket
            data['records'].append(row)
            with self.assertRaises(ValueError): gen.validate(data)

    def test_invalid_records(self):
        for key, value in [('vendor', 'bogus'), ('socket', ''), ('model', None), ('source', 'missing'), ('match', 'fuzzy'), ('status', 'inherited'), ('display_codename', '\n')]:
            with self.subTest(key=key):
                data = copy.deepcopy(self.data)
                data['records'][-1][key] = value
                with self.assertRaises(ValueError): gen.validate(data)

    def test_schema(self):
        self.data['schema_version'] = 2
        with self.assertRaises(ValueError): gen.validate(self.data)

    def test_provenance_date(self):
        self.data['sources']['amd-7945hx']['retrieved'] = 'yesterday'
        with self.assertRaises(ValueError): gen.validate(self.data)

    def test_no_legacy_codename_override(self):
        self.data['records'][0]['display_codename'] = 'Other'
        with self.assertRaises(ValueError): gen.validate(self.data)

    def test_unknown_fields(self):
        self.data['records'][0]['sockte'] = 'Typo'
        with self.assertRaises(ValueError): gen.validate(self.data)


if __name__ == '__main__': unittest.main()
