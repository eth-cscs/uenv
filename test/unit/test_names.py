import shutil
import unittest

import names

class TestUenvParsing(unittest.TestCase):

    def test_parse_uenv_string(self):
        self.assertEqual(names.parse_uenv_string('prgenv-gnu'),
                         {'name': 'prgenv-gnu', 'version': None, 'tag': None, 'sha': None})
        self.assertEqual(names.parse_uenv_string('prgenv-gnu/23.2'),
                         {'name': 'prgenv-gnu', 'version': '23.2', 'tag': None, 'sha': None})
        self.assertEqual(names.parse_uenv_string('prgenv-gnu/23.2:v2-rc1'),
                         {'name': 'prgenv-gnu', 'version': '23.2', 'tag': 'v2-rc1', 'sha': None})
        self.assertEqual(names.parse_uenv_string('prgenv-gnu:default'),
                         {'name': 'prgenv-gnu', 'version': None, 'tag': 'default', 'sha': None})
        self.assertEqual(names.parse_uenv_string('prgenv-gnu:default'),
                         {'name': 'prgenv-gnu', 'version': None, 'tag': 'default', 'sha': None})
        self.assertEqual(names.parse_uenv_string('1736b4bb5ad9b3c5cae8878c71782a8bf2f2f739dbce8e039b629de418cb4dab'),
                         {'name': None, 'version': None, 'tag': None, 'sha': '1736b4bb5ad9b3c5cae8878c71782a8bf2f2f739dbce8e039b629de418cb4dab'})
        self.assertEqual(names.parse_uenv_string('1736b4bb5ad9b3c5'),
                         {'name': None, 'version': None, 'tag': None, 'sha': '1736b4bb5ad9b3c5'})
