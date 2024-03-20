import unittest

import datastore
import record

class TestInMemory(unittest.TestCase):

    def setUp(self):
        self.store = datastore.DataStore(path=None)
        record1 = record.Record("santis", "gh200", "prgenv-gnu", "23.11", "latest", "monday", 1024,
                                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
        record2 = record.Record("santis", "gh200", "prgenv-gnu", "23.11", "v2", "monday", 1024,
                                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
        record3 = record.Record("santis", "gh200", "prgenv-gnu", "23.11", "v1", "monday", 1024,
                                "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")
        record4 = record.Record("santis", "gh200", "prgenv-gnu", "24.2", "latest", "monday", 1024,
                                "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")
        record5 = record.Record("santis", "gh200", "prgenv-gnu", "24.2", "v1", "monday", 1024,
                                "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")

        self.store.add_record(record1)
        self.store.add_record(record2)
        self.store.add_record(record3)
        self.store.add_record(record4)
        self.store.add_record(record5)

    def tearDown(self):
        """Call after every test case."""
        pass

    def test_foo(self):
        self.store.dump()

    def test_bar(self):
        assert "robert" == "robert"

if __name__ == '__main__':
    unittest.main()
