import shutil
import unittest

import datastore
import record
import scratch

prgenvgnu_records = [
    record.Record("santis", "gh200", "prgenv-gnu", "23.11", "latest", "monday", 1024, "a"*64),
    record.Record("santis", "gh200", "prgenv-gnu", "23.11", "v2",     "monday", 1024, "a"*64),
    record.Record("santis", "gh200", "prgenv-gnu", "23.11", "v1",     "monday", 1024, "c"*64),
    record.Record("santis", "gh200", "prgenv-gnu", "24.2",  "latest", "monday", 1024, "b"*64),
    record.Record("santis", "gh200", "prgenv-gnu", "24.2",  "v1",     "monday", 1024, "b"*64),
]

def create_prgenv_repo(con):
    # add some records that will be inserted into the database
    # these defined different versions of prgenv-gnu
    # there are three unique images, two of which have two tags applied
    for r in prgenvgnu_records:
        con.add_record(r)

class TestInMemory(unittest.TestCase):

    def setUp(self):
        # create an in memory datastore
        self.store = datastore.DataStore(path=None)

        create_prgenv_repo(self.store)

    def tearDown(self):
        """Call after every test case."""
        pass

    def test_contents(self):
        store = self.store
        #store.dump()

        results = self.store.find_records(sha="a"*64)
        self.assertEqual(2, len(results))
        self.assertEqual("latest", results[0].tag)
        self.assertEqual("v2", results[1].tag)

        results = self.store.find_records(sha="b"*64)
        self.assertEqual(2, len(results))
        self.assertEqual("latest", results[0].tag)
        self.assertEqual("v1", results[1].tag)

        results = self.store.find_records(sha="c"*64)
        self.assertEqual(1, len(results))
        self.assertEqual("v1", results[0].tag)

    def test_find_records(self):
        # TODO test different search criteria
        pass

    def test_get_record(self):
        store = self.store

        # test that an invalid sha raises an error
        with self.assertRaises(ValueError):
            result = store.get_record("z"*64)

        # the record with sha f*64 is not in the repo
        result = store.get_record("f"*64)
        self.assertEqual(0, len(result))

        # the record with sha c*64 occurs only once
        result = store.get_record("c"*16)
        self.assertEqual(len(result), 1)
        r_short = result[0]
        result = store.get_record("c"*64)
        r_long = result[0]
        self.assertEqual(r_short, r_long)
        self.assertEqual(r_short.sha256, "c"*64)

        # the record with sha a*64 occurs twice
        result = store.get_record("a"*16)
        self.assertEqual(len(result), 2)
        for i in range(len(result)):
            r_short = result[i]
            result = store.get_record("a"*64)
            r_long = result[i]
            self.assertEqual(r_short, r_long)
            self.assertEqual(r_short.sha256, "a"*64)

    def test_replace_tag(self):
        v2 = record.Record("santis", "gh200", "prgenv-gnu", "24.2", "v2", "monday", 1024,
                           "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd")
        latest = record.Record("santis", "gh200", "prgenv-gnu", "24.2", "latest", "monday", 1024,
                           "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd")

        # before insertion of the new records, the sha bbb... matches two tags: latest and v1
        results = self.store.find_records(sha="b"*64)
        self.assertEqual(2, len(results))

        self.store.add_record(v2)
        self.store.add_record(latest)
        # after insertion of the new record, the sha bbb... matches one tag: v1
        results = self.store.find_records(sha="b"*64)
        self.assertEqual(1, len(results))
        self.assertEqual("v1", results[0].tag)

        # after insertion of the new record, the sha ddd... matches two tags: latest and v2
        results = self.store.find_records(sha="d"*64)
        self.assertEqual(2, len(results))
        self.assertEqual("latest", results[0].tag)
        self.assertEqual("v2", results[1].tag)

class TestRepositoryCreate(unittest.TestCase):

    def setUp(self):
        self.path = scratch.make_scratch_path("repository_create")

    def test_create_new_repo(self):
        datastore.FileSystemRepo.create(self.path.as_posix())
        repo = datastore.FileSystemRepo(self.path.as_posix())
        record = prgenvgnu_records[0]
        repo.add_record(record)
        sha = record.sha256
        self.assertEqual(repo.get_record(sha)[0].sha256, sha)

    def tearDown(self):
        shutil.rmtree(self.path, ignore_errors=True)
        pass


# to test:
#   that no database existing is handled correctly
#       - path already exists
#       - path does not exist
#   that repository creation works correctly
#   that adding records to a repository works (and that after the databse has been saved, the correct state is persisted)
#   when an image with >1 tags is pulled from JFrog, all tags are added to the databse correctly

if __name__ == '__main__':
    unittest.main()
