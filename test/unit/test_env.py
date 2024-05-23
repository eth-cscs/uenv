import shutil
import unittest

import envvars

class TestListEnvVars(unittest.TestCase):

    def test_env_var_name(self):
        # test that the name is set correctly
        self.assertEqual("PATH", envvars.ListEnvVar("PATH", ["/foo/bin"], envvars.EnvVarOp.SET).name)

    def test_env_var_list_shor(self):
        self.assertEqual(
            "/foo/bin:/bar/bin",
            envvars.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], envvars.EnvVarOp.SET).get_value(None))
        self.assertEqual(
            "/foo/bin:/bar/bin",
            envvars.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], envvars.EnvVarOp.SET).get_value(""))
        self.assertEqual(
            "/foo/bin:/bar/bin",
            envvars.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], envvars.EnvVarOp.SET).get_value("/wombat"))
        self.assertEqual(
            "/foo/bin:/bar/bin",
            envvars.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], envvars.EnvVarOp.PREPEND).get_value(None))
        self.assertEqual(
            "/foo/bin:/bar/bin",
            envvars.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], envvars.EnvVarOp.PREPEND).get_value(""))
        self.assertEqual(
            "/foo/bin:/bar/bin:/wombat",
            envvars.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], envvars.EnvVarOp.PREPEND).get_value("/wombat"))
        self.assertEqual(
            "/foo/bin:/bar/bin",
            envvars.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], envvars.EnvVarOp.APPEND).get_value(None))
        self.assertEqual(
            "/foo/bin:/bar/bin",
            envvars.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], envvars.EnvVarOp.APPEND).get_value(""))
        self.assertEqual(
            "/wombat:/foo/bin:/bar/bin",
            envvars.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], envvars.EnvVarOp.APPEND).get_value("/wombat"))

    def test_env_var_list_long(self):
        v = envvars.ListEnvVar("PATH", ["c"], envvars.EnvVarOp.PREPEND)
        v.update(["a","b"], envvars.EnvVarOp.PREPEND)
        v.update(["e","f"], envvars.EnvVarOp.APPEND)
        self.assertEqual("a:b:c:d:e:f", v.get_value("d"))
        self.assertEqual("a:b:c:e:f", v.get_value(None))

        v = envvars.ListEnvVar("PATH", ["c"], envvars.EnvVarOp.SET)
        v.update(["a","b"], envvars.EnvVarOp.PREPEND)
        v.update(["e","f"], envvars.EnvVarOp.APPEND)
        self.assertEqual("a:b:c:e:f", v.get_value("d"))

        self.assertEqual("a:b:c:e:f", v.get_value(None))

    def test_env_var_list_concat(self):
        v  = envvars.ListEnvVar("PATH", ["a"], envvars.EnvVarOp.PREPEND)
        x = envvars.ListEnvVar("PATH", ["c"], envvars.EnvVarOp.APPEND)
        v.concat(x)
        self.assertEqual("a:b:c", v.get_value("b"))

    def test_env_var_scalars(self):
        v = envvars.ScalarEnvVar("HOME", "/users/bob")
        self.assertEqual("HOME", v.name)
        self.assertEqual("/users/bob", v.value)

        v = envvars.ScalarEnvVar("HOME", None)
        self.assertEqual("HOME", v.name)
        self.assertEqual(None, v.value)

class TestEnvVarCategories(unittest.TestCase):

    def test_env_var_categories(self):
        self.assertTrue(envvars.is_list_var("LD_LIBRARY_PATH"))
        self.assertTrue(envvars.is_list_var("PKG_CONFIG_PATH"))
        self.assertFalse(envvars.is_list_var("HOME"))

class TestEnv(unittest.TestCase):

    def test_env(self):
        e = envvars.EnvVarSet()

        e.set_scalar("HOME", "/users/bob")
        e.set_scalar("VISIBLE", "")
        e.set_scalar("NO", None)

        e.set_list("PATH", ["a"], envvars.EnvVarOp.PREPEND)
        e.set_list("PATH", ["c"], envvars.EnvVarOp.APPEND)

        self.assertTrue("HOME" in e.scalars.keys())
        self.assertEqual("/users/bob", e.scalars["HOME"].value)
        self.assertTrue("VISIBLE" in e.scalars.keys())
        self.assertEqual("", e.scalars["VISIBLE"].value)
        self.assertTrue("NO" in e.scalars.keys())
        self.assertTrue(e.scalars["NO"].is_null)
        self.assertTrue("PATH" in e.lists.keys())
        self.assertEqual("a:b:c", e.lists["PATH"].get_value("b"))
