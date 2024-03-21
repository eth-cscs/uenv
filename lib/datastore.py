import json
import os
import sqlite3

from record import Record
import terminal
import names

UENV_CLI_API_VERSION=1

create_db_command = """
BEGIN;

PRAGMA foreign_keys=on;

CREATE TABLE images (
    sha256 TEXT PRIMARY KEY CHECK(length(sha256)==64),
    short_sha TEXT UNIQUE CHECK(length(short_sha)==16),
    date TEXT NOT NULL,
    size INTEGER NOT NULL,
    uarch TEXT NOT NULL,
    system TEXT NOT NULL
);

CREATE TABLE uenv (
    version_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    version TEXT NOT NULL,
    UNIQUE (name, version)
);

CREATE TABLE tags (
    version_id INTEGER,
    tag TEXT NOT NULL,
    sha256 TEXT NOT NULL,
    PRIMARY KEY (version_id, tag),
    FOREIGN KEY (version_id)
        REFERENCES uenv (version_id)
            ON DELETE CASCADE
            ON UPDATE CASCADE,
    FOREIGN KEY (sha256)
        REFERENCES images (sha256)
            ON DELETE CASCADE
            ON UPDATE CASCADE
);

-- for convenient generation of the Record type used internally by uenv-image
CREATE VIEW records AS
SELECT
    images.system    AS system,
    images.uarch     AS uarch,
    uenv.name        AS name,
    uenv.version     AS version,
    tags.tag         AS tag,
    images.date      AS date,
    images.size      AS size,
    tags.sha256      AS sha256,
    images.short_sha AS short_sha
FROM tags
    INNER JOIN uenv   ON uenv.version_id = tags.version_id
    INNER JOIN images ON images.sha256   = tags.sha256;

COMMIT;
"""

class DataStore:

    def __init__(self, path=None):
        """
        If path is a string, attempt to open the database at that location,

        Otherwise creat a new empty in-memory database.

        raises an execption if opening an on-disk database and the file is not
        found, or if sqlite3 raises and execption when opening the databse.
        """
        if path is None:
            self._store = sqlite3.connect(":memory:")
            self._store.executescript(create_db_command)
        else:
            if not os.path.isfile(path):
                raise FileNotFoundError(f"repository database file {path} does not exist")
            self._store = sqlite3.connect(path)

    def add_record(self, r: Record):
        """
        Add a record to the database.
        """

        short_sha = r.sha256[:16]

        cursor = self._store.cursor()

        cursor.execute("BEGIN;")
        cursor.execute("PRAGMA foreign_keys=on;")
        cursor.execute("INSERT OR IGNORE INTO images (sha256, short_sha, date, size, uarch, system) VALUES (?, ?, ?, ?, ?, ?)",
                       (r.sha256, short_sha, r.date, r.size, r.uarch, r.system))
        # Insert a new name/version to the uenv table if no existing images with that pair exist
        cursor.execute("INSERT OR IGNORE INTO uenv (name, version) VALUES (?, ?)",
                       (r.name, r.version))
        # Retrieve the version_id of the name/version pair
        # This requires a SELECT query to get the correct version_id whether or not
        # a new row was added in the last INSERT
        cursor.execute("SELECT version_id FROM uenv WHERE name = ? AND version = ?",
                       (r.name, r.version))
        version_id = cursor.fetchone()[0]
        # Check whether an image with the same tag already exists in the repos
        cursor.execute("SELECT version_id, tag, sha256 FROM tags WHERE version_id = ? AND tag = ?",
                       (version_id, r.tag))
        existing_tag = cursor.fetchone()
        # If the tag exists, update the entry in the table with the new sha256
        if existing_tag:
            cursor.execute("UPDATE tags SET sha256 = ? WHERE version_id = ? AND tag = ?",
                           (r.sha256, version_id, r.tag))
        # Else add a new row
        else:
            cursor.execute("INSERT INTO tags (version_id, tag, sha256) VALUES (?, ?, ?)",
                           (version_id, r.tag, r.sha256))
        # Commit the transaction
        self._store.commit()

    def dump(self):
        """
        Dump the contents of the internal database to stdout.
        For debugging purposes only.
        """
        uenv_tbl = self._store.execute("SELECT * FROM uenv")
        image_tbl = self._store.execute("SELECT * FROM images")
        tags_tbl = self._store.execute("SELECT * FROM tags")
        records_tbl = self._store.execute("SELECT * FROM records")

        print()
        print("== TABLE images ==")
        print(f"{'sha256':64s} {'id':16s} {'date':10s} {'size':11s} {'uearch':8s} {'system':12s}")
        for line in image_tbl:
            print(f"{line[0]:64s} {line[1]:16s} {line[2]:10s} {line[3]:11d} {line[4]:8s} {line[5]:12s}")
        print()
        print("== TABLE uenv ==")
        print(f"{'id':4s} {'name':16s} {'version':16s}")
        for line in uenv_tbl:
            print(f"{line[0]:4d} {line[1]:16s} {line[2]:16s}")
        print()
        print("== TABLE tags ==")
        print(f"{'id':4s} {'tag':16s} {'sha256':16s}")
        for line in tags_tbl:
            print(f"{line[0]:4d} {line[1]:16s} {line[2]:64s}")
        print()
        print("== TABLE records ==")
        for line in records_tbl:
            print(f"{line}")

    def to_record(self, r):
        return Record(r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7])

    def find_records(self, **constraints):
        if not constraints:
            raise ValueError("At least one constraint must be provided")

        for field in constraints:
            if (field != "sha") and (field not in ['system', 'uarch', 'name', 'version', 'tag', 'sha']):
                raise ValueError(f"Invalid field: {field}. Must be one of 'system', 'uarch', 'name', 'version', 'tag', 'sha'")

        if "sha" in constraints:
            sha = constraints["sha"]

            if len(sha)<64:
                items = self._store.execute(f"SELECT * FROM records WHERE short_sha = '{sha}'")
            else:
                items = self._store.execute(f"SELECT * FROM records WHERE sha256 = '{sha}'")
        else:
            query_criteria = " AND ".join([f"{field} = '{value}'"
                                           for field, value in constraints.items()])

            # Find matching records for each constraint
            items = self._store.execute(f"SELECT * FROM records WHERE {query_criteria}")

        results = [self.to_record(r) for r in items];
        results.sort(reverse=True)
        return results

    @property
    def images(self):
        return self._images

    # return a list of records that match a sha
    def get_record(self, sha: str) -> Record:
        """
        Return a list of records that match the sha (either full 64 or short 16 character sha)
        If there are no matches, an empty list is returned.
        If an invalid sha is passed, a ValueError is raised.
        """
        if not names.is_valid_sha(sha):
            raise ValueError(f"{sha} is not a valid sha256 (neither full 64 or short 16 character form)")
        if names.is_full_sha256(sha):
            results = self._store.execute(f"SELECT * FROM records WHERE sha256 = '{sha}'")
        elif names.is_short_sha256(sha):
            results = self._store.execute(f"SELECT * FROM records WHERE short_sha  = '{sha}'")

        return [self.to_record(r) for r in results]

    # Convert to a dictionary that can be written to file as JSON
    # The serialisation and deserialisation are central: able to represent
    # uenv that are available in both JFrog and filesystem directory tree.
    #def serialise(self, version: int=UENV_CLI_API_VERSION):
    #    image_list = []
    #    for x in self._images.values():
    #        image_list += x
    #    terminal.info(f"serialized image list in datastore: {image_list}")
    #    return {
    #            "API_VERSION": version,
    #            "images": [img.dictionary for img in image_list]
    #    }

    # Convert to a dictionary that can be written to file as JSON
    # The serialisation and deserialisation are central: able to represent
    # uenv that are available in both JFrog and filesystem directory tree.
    #@classmethod
    #def deserialise(cls, datastore):
    #    result = cls()
    #    for img in datastore["images"]:
    #        result.add_record(Record.from_dictionary(img))

class FileSystemRepo():
    def __init__(self, path: str):
        self._path = path
        self._index = path + "/index.db"

        if not os.path.exists(self._path):
            # error: repository does not exists
            raise FileNotFoundError(f"uenv image repository not found {self._path}: the path does not exist.")

        if not os.path.exists(self._index):
            # error: index does not exists
            raise FileNotFoundError(f"uenv image repository not found: the index database is missing {self._index}.")

        self._database = DataStore(self._index)

    @staticmethod
    def create(path: str, exists_ok: bool=False):
        terminal.info(f"FileSystemRepo: create new repo in {path}")
        if not os.path.exists(path):
            terminal.info(f"FileSystemRepo: creating path {path}")
            os.makedirs(path)

        index_file = f"{path}/index.db"
        if not os.path.exists(index_file):
            terminal.info(f"FileSystemRepo: creating new index {index_file}")
            store = sqlite3.connect(index_file)
            store.executescript(create_db_command)
        elif not exists_ok:
            raise FileExistsError(f"A repository already exists in the location {path}.")

        terminal.info(f"FileSystemRepo: created empty repository with database {index_file}")

    @property
    def database(self):
        return self._database

    def add_record(self, record: Record):
        self._database.add_record(record)

    # The path where an image would be stored
    # will return a path even for images that are not stored
    def image_path(self, r: Record) -> str:
        return self._path + "/images/" + r.sha256

    # Return the full record for a given hash
    # Returns None if no image with that hash is stored in the repo.
    def get_record(self, sha256: str):
        """
        Return a list of records that match the sha256 (either full 64 or short 16 character sha)
        If there are no matches, an empty list is returned.
        If an invalid sha is passed, a ValueError is raised.
        """
        return self._database.get_record(sha256)


