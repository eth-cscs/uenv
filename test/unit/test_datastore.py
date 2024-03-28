import shutil
import unittest

import datastore
import record
import scratch

prgenvgnu_records = [
    record.Record("santis", "gh200", "prgenv-gnu", "23.11", "default", "monday", 1024, "a"*64),
    record.Record("santis", "gh200", "prgenv-gnu", "23.11", "v2",     "monday", 1024, "a"*64),
    record.Record("santis", "gh200", "prgenv-gnu", "23.11", "v1",     "monday", 1024, "c"*64),
    record.Record("santis", "gh200", "prgenv-gnu", "24.2",  "default", "monday", 1024, "b"*64),
    record.Record("santis", "gh200", "prgenv-gnu", "24.2",  "v1",     "monday", 1024, "b"*64),
]

icon_records = [
    record.Record("santis", "gh200", "icon", "2024", "default", "2024/02/12", 1024, "1"*64),
    record.Record("santis", "gh200", "icon", "2024", "v2",     "2024/02/12", 1024, "1"*64),
    record.Record("santis", "gh200", "icon", "2024", "v1",     "2023/01/01", 5024, "2"*64),
]

def create_prgenv_repo(con):
    # add some records that will be inserted into the database
    # these defined different versions of prgenv-gnu
    # there are three unique images, two of which have two tags applied
    for r in prgenvgnu_records:
        con.add_record(r)

def create_full_repo(con):
    # create a repo with icon and prgenv-gnu images
    # there are three unique images, two of which have two tags applied
    for r in prgenvgnu_records:
        con.add_record(r)

    for r in icon_records:
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
        records = results.records
        self.assertTrue(results.is_unique_sha)
        self.assertEqual(2, len(records))
        self.assertEqual("v2", records[0].tag)
        self.assertEqual("default", records[1].tag)

        results = self.store.find_records(sha="b"*64)
        records = results.records
        self.assertTrue(results.is_unique_sha)
        self.assertFalse(results.is_empty)
        self.assertFalse(results.is_unique_record)
        self.assertEqual(2, len(records))
        self.assertEqual("v1", records[0].tag)
        self.assertEqual("default", records[1].tag)

        results = self.store.find_records(sha="c"*64)
        records = results.records
        self.assertTrue(results.is_unique_sha)
        self.assertFalse(results.is_empty)
        self.assertTrue(results.is_unique_record)
        self.assertEqual(1, len(records))
        self.assertEqual("v1", records[0].tag)

    def test_find_records(self):
        store = datastore.DataStore(path=None)
        create_full_repo(store)

        self.assertEqual(3, len(store.find_records(name="icon").records))
        self.assertEqual(5, len(store.find_records(name="prgenv-gnu").records))

        self.assertEqual(1, len(store.find_records(name="icon", tag="v1").records))
        self.assertEqual(1, len(store.find_records(name="icon", tag="v2").records))
        self.assertEqual(1, len(store.find_records(name="icon", tag="default").records))
        self.assertEqual(0, len(store.find_records(name="icon", tag="happydays").records))

        self.assertEqual(2, len(store.find_records(name="prgenv-gnu", tag="v1").records))
        self.assertEqual(1, len(store.find_records(name="prgenv-gnu", tag="v2").records))

        self.assertEqual(3, len(store.find_records(tag="v1").records))
        self.assertEqual(2, len(store.find_records(tag="v2").records))
        self.assertEqual(3, len(store.find_records(tag="default").records))

        self.assertEqual(3, len(store.find_records(version="2024").records))
        self.assertEqual(3, len(store.find_records(version="23.11").records))
        self.assertEqual(0, len(store.find_records(name="icon", version="23.11").records))
        self.assertEqual(3, len(store.find_records(name="prgenv-gnu", version="23.11").records))

        self.assertEqual(3, len(store.find_records(name="icon", version="2024").records))
        self.assertEqual(1, len(store.find_records(name="icon", version="2024", tag="default").records))
        self.assertEqual(1, len(store.find_records(name="icon", version="2024", tag="v1").records))
        self.assertEqual(1, len(store.find_records(name="icon", version="2024", tag="v2").records))
        self.assertEqual(store.find_records(name="icon", version="2024", tag="default").records,
                         store.find_records(name="icon", version="2024", tag="v2").records)

        self.assertEqual(8, len(store.find_records(uarch="gh200").records))
        self.assertEqual(8, len(store.find_records(system="santis").records))

        self.assertEqual(store.find_records(system="santis").records,
                         store.find_records(uarch="gh200").records)

        # expect an exception when an invalid field is passed (sustem is a typo for system)
        with self.assertRaises(ValueError):
            result = store.find_records(sustem="santis")

    def test_get_record(self):
        store = self.store

        # test that an invalid sha raises an error
        with self.assertRaises(ValueError):
            result = store.get_record("z"*64)

        # the record with sha f*64 is not in the repo
        result = store.get_record("f"*64)
        self.assertTrue(result.is_empty)

        # the record with sha c*64 occurs only once
        result = store.get_record("c"*16)
        self.assertTrue(result.is_unique_record)
        # check that looking up using the short sha gives the same result as the long sha
        r_short = result.records[0]
        result = store.get_record("c"*64)
        r_long = result.records[0]
        self.assertEqual(r_short, r_long)
        self.assertEqual(r_short.sha256, "c"*64)

        # the record with sha a*64 occurs twice
        result = store.get_record("a"*16)
        self.assertFalse(result.is_unique_record)
        self.assertTrue(result.is_unique_sha)
        self.assertEqual(len(result.records), 2)
        for i in range(2):
            r_short = result.records[i]
            result = store.get_record("a"*64)
            r_long = result.records[i]
            self.assertEqual(r_short, r_long)
            self.assertEqual(r_short.sha256, "a"*64)

    def test_replace_tag(self):
        v2 = record.Record("santis", "gh200", "prgenv-gnu", "24.2", "v2", "monday", 1024,
                           "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd")
        latest = record.Record("santis", "gh200", "prgenv-gnu", "24.2", "default", "monday", 1024,
                           "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd")

        # before insertion of the new records, the sha bbb... matches two tags: default and v1
        results = self.store.find_records(sha="b"*64)
        self.assertEqual(2, len(results.records))

        self.store.add_record(v2)
        self.store.add_record(latest)
        # after insertion of the new record, the sha bbb... matches one tag: v1
        results = self.store.find_records(sha="b"*64)
        self.assertTrue(results.is_unique_record)
        self.assertEqual("v1", results.records[0].tag)

        # after insertion of the new record, the sha ddd... matches two tags: default and v2
        results = self.store.find_records(sha="d"*64)
        records = results.records
        self.assertEqual(2, len(records))
        self.assertEqual("v2", records[0].tag)
        self.assertEqual("default", records[1].tag)

class TestInvalidRepository(unittest.TestCase):
    def setUp(self):
        self.path = scratch.make_scratch_path("repository_create")

    def tearDown(self):
        shutil.rmtree(self.path, ignore_errors=True)

    def test_open_non_existing_repo(self):
        rpath = self.path / 'foobar'
        with self.assertRaises(datastore.RepoNotFoundError):
            datastore.FileSystemRepo(rpath.as_posix())

    def test_open_non_existing_db(self):
        rpath = self.path / 'foobar'
        rpath.mkdir()
        with self.assertRaises(datastore.RepoNotFoundError):
            datastore.FileSystemRepo(rpath.as_posix())

    def test_open_corrupt_db(self):
        rpath = self.path / 'repo'
        dbpath = rpath / 'index.db'
        rpath.mkdir(parents=False, exist_ok=False)
        # create an invalid database file
        with open(dbpath, 'w') as f:
            f.write("corruption and decay")


        with self.assertRaises(datastore.RepoDBError):
            repo = datastore.FileSystemRepo(rpath.as_posix())

class TestRepositoryCreate(unittest.TestCase):

    def setUp(self):
        self.path = scratch.make_scratch_path("repository_create")

    def test_create_new_repo(self):
        # expect an exception when an invalid field is passed (sustem is a typo for system)
        with self.assertRaises(ValueError):
            result = store.find_records(sustem="santis")

    def test_create_new_repo(self):
        # create a new repository with database on disk
        datastore.FileSystemRepo.create(self.path.as_posix())

        # verify that the file was created
        self.assertTrue((self.path / "index.db").is_file())

        # now open a connection to the repository
        repo = datastore.FileSystemRepo(self.path.as_posix())
        record = prgenvgnu_records[0]
        repo.add_record(record)
        sha = record.sha256
        self.assertEqual(repo.database.get_record(sha).records[0].sha256, sha)

    def tearDown(self):
        shutil.rmtree(self.path, ignore_errors=True)

if __name__ == '__main__':
    unittest.main()
