import json
import os
import shutil
import sqlite3

from record import Record
import terminal
import names

UENV_CLI_API_VERSION=1

class RepoNotFoundError(Exception):
    """Exception raised when unable to open a FileSystem repository."""

    def __init__(self, message):
        self.message = message
        super().__init__(self.message)

    def __str__(self):
        return self.message

class RepoDBError(Exception):
    """Exception raised when there is an internal sqlite3 error."""

    def __init__(self, message):
        self.message = message
        super().__init__(self.message)

    def __str__(self):
        return self.message

create_db_commands = {
        1: """
BEGIN;

PRAGMA foreign_keys=on;

CREATE TABLE images (
    sha256 TEXT PRIMARY KEY CHECK(length(sha256)==64),
    id TEXT UNIQUE CHECK(length(id)==16),
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
    images.system  AS system,
    images.uarch   AS uarch,
    uenv.name      AS name,
    uenv.version   AS version,
    tags.tag       AS tag,
    images.date    AS date,
    images.size    AS size,
    tags.sha256    AS sha256,
    images.id      AS id
FROM tags
    INNER JOIN uenv   ON uenv.version_id = tags.version_id
    INNER JOIN images ON images.sha256   = tags.sha256;

COMMIT;
""",
        2: """
BEGIN;

PRAGMA foreign_keys=on;

CREATE TABLE images (
    sha256 TEXT PRIMARY KEY CHECK(length(sha256)==64),
    id TEXT UNIQUE CHECK(length(id)==16),
    date TEXT NOT NULL,
    size INTEGER NOT NULL
);

CREATE TABLE uenv (
    version_id INTEGER PRIMARY KEY,
    system TEXT NOT NULL,
    uarch TEXT NOT NULL,
    name TEXT NOT NULL,
    version TEXT NOT NULL,
    UNIQUE (system, uarch, name, version)
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
    uenv.system  AS system,
    uenv.uarch   AS uarch,
    uenv.name      AS name,
    uenv.version   AS version,
    tags.tag       AS tag,
    images.date    AS date,
    images.size    AS size,
    tags.sha256    AS sha256,
    images.id      AS id
FROM tags
    INNER JOIN uenv   ON uenv.version_id = tags.version_id
    INNER JOIN images ON images.sha256   = tags.sha256;

COMMIT;
"""}

# the version schema is an integer, bumped every time it is modified
db_version = 2
create_db_command = create_db_commands[db_version]

# returns the version of the database in repo_path
#  returns -1 if there was an error or unknown database format
def repo_version(repo_path: str):
    try:
        # open the suspect database read only
        db_path = f"{repo_path}/index.db"
        db = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
        result = db.execute("SELECT * FROM images")
        columns = [description[0] for description in result.description]
        columns.sort()
        db.close()

        # the image tables in v1 and v2 have the respective columns:
        # v1: images (date, id, sha256, size, system, uarch);
        # v2: images (date, id, sha256, size);

        version = -1
        if columns == ["date", "id", "sha256", "size"]:
            version = 2
        elif columns == ["date", "id", "sha256", "size", "system", "uarch"]:
            version = 1
        terminal.info(f"database {db_path} matches schema version {version}")

        return version
    except Exception as err:
        terminal.info(f"exception opening {db_path}: {str(err)}")
        return -1

# return 2: repo does not exist
# return 1: repo is fully up to date
# return 0: repo needs upgrading
# return -1: unrecoverable error
def repo_status(repo_path: str):
    index_path = repo_path + "/index.db"
    if not os.path.exists(index_path):
        return 2

    version = repo_version(repo_path)

    if version==db_version:
        return 1
    elif version==1:
        return 0

    return -1

def repo_upgrade(repo_path: str):
    version = repo_version(repo_path)
    if version==2:
        return
    elif version==-1:
        raise RepoDBError("unable to upgrade database due to fatal error.")

    if version==1:
        # load the existing database using the v1 schema
        db1_path = repo_path + "/index.db"
        db1 = DataStore(db1_path)

        # create a new database with the v2 schema
        db2_path = repo_path + "/index-v1.db"
        if os.path.exists(db2_path):
            os.remove(db2_path)
        FileSystemRepo.create(repo_path, exists_ok=False, db_name="index-v1.db")

        # apply the records from the old database to the new db
        db2 = DataStore(db2_path, create_command=create_db_commands[2])
        for r in db1.images.records:
            db2.add_record(r)

        # close the databases
        db1.close()
        db2.close()

        dbswp_path = repo_path + "/index-back.db"
        shutil.move(db1_path, dbswp_path)
        shutil.copy(db2_path, db1_path)
        shutil.move(dbswp_path, db2_path)


class RecordSet():
    def __init__(self, records, request):
        self._shas = set([r.sha256 for r in records])
        self._records = records
        sha2record = {}
        for sha in self._shas:
            sha2record[sha] = [r for r in records if r.sha256==sha]
        self._sha2record = sha2record
        self._request = request

    @property
    def shas(self):
        return list(self._shas)

    @property
    def records(self):
        return self._records

    @property
    def request(self):
        return self._request

    @property
    def sha2record(self):
        return self._sha2record

    @property
    def is_unique_sha(self):
        return len(self._shas)==1

    @property
    def is_unique_record(self):
        return len(self._records)==1

    @property
    def is_empty(self):
        return len(self._shas)==0

    def ambiguous_request_message(self):
        """
        Generate the message to print when a request returns an ambiguous result.
        """
        lines = []
        lines = [f"more than one uenv matches the spec {terminal.colorize(self.request, 'yellow')}",
                 f"the options are:"]
        for r in self._records:
            lines.append(f"  {terminal.colorize(r.full_name, 'white'):34}  {terminal.colorize(r.id, 'cyan')}")

        return lines

class DataStore:
    def __init__(self, path=None, create_command=create_db_command):
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
                raise RepoNotFoundError(f"repository database file {path} does not exist")
            try:
                self._store = sqlite3.connect(path)
            except Exception as err:
                raise RepoDBError(str(err))

            # Ppening a connection does not check the validity of the database.
            # Run a query to check that the database is valid.
            try:
                _ = self._store.execute("SELECT * FROM records")
            except Exception as err:
                raise RepoDBError(str(err))


    def add_record(self, r: Record):
        """
        Add a record to the database.
        """

        cursor = self._store.cursor()

        cursor.execute("BEGIN;")
        cursor.execute("PRAGMA foreign_keys=on;")
        cursor.execute("INSERT OR IGNORE INTO images (sha256, id, date, size) VALUES (?, ?, ?, ?)",
                       (r.sha256, r.id, r.date, r.size))
        # Insert a new system/uarch/name/version to the uenv table if no existing images exist
        cursor.execute("INSERT OR IGNORE INTO uenv (system, uarch, name, version) VALUES (?, ?, ?, ?)",
                       (r.system, r.uarch, r.name, r.version))
        # Retrieve the version_id of the system/uarch/name/version identifier
        # This requires a SELECT query to get the correct version_id whether or not
        # a new row was added in the last INSERT
        cursor.execute("SELECT version_id FROM uenv WHERE system = ? AND uarch = ? AND name = ? AND version = ?",
                       (r.system, r.uarch, r.name, r.version))
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
                items = self._store.execute(f"SELECT * FROM records WHERE id = '{sha}'")
            else:
                items = self._store.execute(f"SELECT * FROM records WHERE sha256 = '{sha}'")

            request = sha
        else:
            query_criteria = " AND ".join([f"{field} = '{value}'"
                                           for field, value in constraints.items()])

            # Find matching records for each constraint
            items = self._store.execute(f"SELECT * FROM records WHERE {query_criteria}")

            ns = constraints.get("name",    "*")
            vs = constraints.get("version", "*")
            ts = constraints.get("tag",     "*")
            us = constraints.get("uarch",   "*")
            ss = constraints.get("system",  "all")
            request = f"{ns}/{vs}:{ts}@{us} on {ss}"

        results = [self.to_record(r) for r in items];
        results.sort(reverse=True)
        return RecordSet(results, request)

    def close(self):
        self._store.close()

    @property
    def images(self):
        items = self._store.execute(f"SELECT * FROM records")
        return RecordSet([self.to_record(r) for r in items], "{*}/{*}:{*}@{*} on all")

    # return a list of records that match a sha
    def get_record(self, sha: str) -> Record:
        """
        Return a list of records that match the sha (either full 64 or 16 char id)
        If there are no matches, an empty list is returned.
        If an invalid sha is passed, a ValueError is raised.
        """
        if not names.is_valid_sha(sha):
            raise ValueError(f"{sha} is not a valid 64 character sha256 or 16 character id")
        if names.is_full_sha256(sha):
            results = self._store.execute(f"SELECT * FROM records WHERE sha256 = '{sha}'")
        elif names.is_id(sha):
            results = self._store.execute(f"SELECT * FROM records WHERE id  = '{sha}'")

        return RecordSet([self.to_record(r) for r in results], sha)

class FileSystemRepo():
    def __init__(self, path: str):
        self._path = path
        self._index = path + "/index.db"

        if not os.path.exists(self._path):
            # error: repository does not exists
            raise RepoNotFoundError(f"uenv image repository not found - the repository path {self._path} does not exist.")

        if not os.path.exists(self._index):
            # error: index does not exists
            raise RepoNotFoundError(f"uenv image repository not found - the index database {self._index} does not exist.")

        self._database = DataStore(self._index)

    @staticmethod
    def create(path: str, exists_ok: bool=False, db_name: str="index.db"):
        terminal.info(f"FileSystemRepo: create new repo in {path}")
        if not os.path.exists(path):
            terminal.info(f"FileSystemRepo: creating path {path}")
            os.makedirs(path)

        index_file = f"{path}/{db_name}"
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

