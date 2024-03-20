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

COMMIT;
"""

class DataStore:

    def __init__(self, path=None):
        """
        if path is a string, attempt to open the database at that location,
        otherwise creat a new in-memory database
        """
        if path is None:
            self._store = sqlite3.connect(":memory:")
            self._store.executescript(create_db_command)
        else:
            self._store = sqlite3.connect(path)

    def add_record(self, r: Record):
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
        for line in self._store.iterdump():
            print(f"{line}")

    def find_records(self, **constraints):
        if not constraints:
            raise ValueError("At least one constraint must be provided")

        for field in constraints:
            if (field != "sha") and (field not in self._store):
                raise ValueError(f"Invalid field: {field}. Must be one of 'system', 'uarch', 'name', 'version', 'tag', 'sha'")

        if "sha" in constraints:
            sha = constraints["sha"]

            #matching_records_sets = [set()]
            #if len(sha)<64:
            #    if sha in self._short_sha:
            #        matching_records_sets = [set([self._short_sha[sha]])]
            #else:
            #    if sha in self._images:
            #        matching_records_sets = [set([sha])]
        else:
            # Find matching records for each constraint
            matching_records_sets = [
                set(self._store[field].get(value, [])) for field, value in constraints.items()
            ]

        # Intersect all sets of matching records
        if matching_records_sets:
            unique = set.intersection(*matching_records_sets)
        else:
            unique = set()

        results = []
        for sha in unique:
            results += (self._images[sha])
        results.sort(reverse=True)
        return results

    @property
    def images(self):
        return self._images

    # return a list of records that match a sha
    def get_record(self, sha: str) -> Record:
        if names.is_full_sha256(sha):
            return self._images.get(sha, [])
        elif names.is_short_sha256(sha):
            return self._images.get(self._short_sha[sha], [])
        raise ValueError(f"{sha} is not a valid sha256 or short (16 character) sha")

    # Convert to a dictionary that can be written to file as JSON
    # The serialisation and deserialisation are central: able to represent
    # uenv that are available in both JFrog and filesystem directory tree.
    def serialise(self, version: int=UENV_CLI_API_VERSION):
        image_list = []
        for x in self._images.values():
            image_list += x
        terminal.info(f"serialized image list in datastore: {image_list}")
        return {
                "API_VERSION": version,
                "images": [img.dictionary for img in image_list]
        }

    # Convert to a dictionary that can be written to file as JSON
    # The serialisation and deserialisation are central: able to represent
    # uenv that are available in both JFrog and filesystem directory tree.
    @classmethod
    def deserialise(cls, datastore):
        result = cls()
        for img in datastore["images"]:
            result.add_record(Record.from_dictionary(img))

class FileSystemCache():
    def __init__(self, path: str):
        self._path = path
        self._index = path + "/index.json"

        if not os.path.exists(self._index):
            # error: cache does not exists
            raise FileNotFoundError(f"filesystem cache not found {self._path}")

        with open(self._index, "r") as fid:
            raw = json.loads(fid.read())
            self._database = DataStore()
            for img in raw["images"]:
                self._database.add_record(Record.fromjson(img))

    @staticmethod
    def create(path: str, exists_ok: bool=False):
        if not os.path.exists(path):
            terminal.info(f"FileSyStemCache: creating path {path}")
            os.makedirs(path)
        index_file = f"{path}/index.json"
        if not os.path.exists(index_file):
            terminal.info(f"FileSyStemCache: creating empty index {index_file}")
            empty_config = { "API_VERSION": UENV_CLI_API_VERSION, "images": [] }
            with open(index_file, "w") as f:
                # default serialisation is str to serialise the pathlib.PosixPath
                f.write(json.dumps(empty_config, sort_keys=True, indent=2, default=str))
                f.write("\n")

        terminal.info(f"FileSyStemCache: available {index_file}")

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
        if not names.is_valid_sha(sha256):
            raise ValueError(f"{sha256} is not a valid image sha256 (neither full 64 or short 16 character form)")
        return self._database.get_record(sha256)

    def publish(self):
        with open(self._index, "w") as f:
            # default serialisation is str to serialise the pathlib.PosixPath
            f.write(json.dumps(self._database.serialise(), sort_keys=True, indent=2, default=str))
            f.write("\n")

