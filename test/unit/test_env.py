import shutil
import unittest

import env

class TestListEnvVars(unittest.TestCase):

    def test_env_var_name(self):
        # test that the name is set correctly
        self.assertEqual("PATH", env.ListEnvVar("PATH", ["/foo/bin"], env.EnvVarOp.SET).name)

    def test_env_var_list_shor(self):
        self.assertEqual(
            "/foo/bin:/bar/bin",
            env.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], env.EnvVarOp.SET).get_value(None))
        self.assertEqual(
            "/foo/bin:/bar/bin",
            env.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], env.EnvVarOp.SET).get_value(""))
        self.assertEqual(
            "/foo/bin:/bar/bin",
            env.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], env.EnvVarOp.SET).get_value("/wombat"))
        self.assertEqual(
            "/foo/bin:/bar/bin",
            env.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], env.EnvVarOp.PREPEND).get_value(None))
        self.assertEqual(
            "/foo/bin:/bar/bin",
            env.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], env.EnvVarOp.PREPEND).get_value(""))
        self.assertEqual(
            "/foo/bin:/bar/bin:/wombat",
            env.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], env.EnvVarOp.PREPEND).get_value("/wombat"))
        self.assertEqual(
            "/foo/bin:/bar/bin",
            env.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], env.EnvVarOp.APPEND).get_value(None))
        self.assertEqual(
            "/foo/bin:/bar/bin",
            env.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], env.EnvVarOp.APPEND).get_value(""))
        self.assertEqual(
            "/wombat:/foo/bin:/bar/bin",
            env.ListEnvVar("PATH", ["/foo/bin", "/bar/bin"], env.EnvVarOp.APPEND).get_value("/wombat"))

    def test_env_var_list_long(self):
        v = env.ListEnvVar("PATH", ["c"], env.EnvVarOp.PREPEND)
        v.update(["a","b"], env.EnvVarOp.PREPEND)
        v.update(["e","f"], env.EnvVarOp.APPEND)
        self.assertEqual("a:b:c:d:e:f", v.get_value("d"))
        self.assertEqual("a:b:c:e:f", v.get_value(None))

        v = env.ListEnvVar("PATH", ["c"], env.EnvVarOp.SET)
        v.update(["a","b"], env.EnvVarOp.PREPEND)
        v.update(["e","f"], env.EnvVarOp.APPEND)
        self.assertEqual("a:b:c:e:f", v.get_value("d"))

        self.assertEqual("a:b:c:e:f", v.get_value(None))

    def test_env_var_list_concat(self):
        v  = env.ListEnvVar("PATH", ["a"], env.EnvVarOp.PREPEND)
        x = env.ListEnvVar("PATH", ["c"], env.EnvVarOp.APPEND)
        v.concat(x)
        self.assertEqual("a:b:c", v.get_value("b"))

    def test_env_var_scalars(self):
        v = env.ScalarEnvVar("HOME", "/users/bob")
        self.assertEqual("HOME", v.name)
        self.assertEqual("/users/bob", v.value)

        v = env.ScalarEnvVar("HOME", None)
        self.assertEqual("HOME", v.name)
        self.assertEqual(None, v.value)

class TestEnvVarCategories(unittest.TestCase):

    def test_env_var_categories(self):
        self.assertTrue(env.is_list_var("LD_LIBRARY_PATH"))
        self.assertTrue(env.is_list_var("PKG_CONFIG_PATH"))
        self.assertFalse(env.is_list_var("HOME"))

class TestEnv(unittest.TestCase):

    def test_env(self):
        e = env.Env()

        e.set_scalar(env.ScalarEnvVar("HOME", "/users/bob"))
        e.set_scalar(env.ScalarEnvVar("VISIBLE", ""))

        print()
        print(e.scalars)
        print()

        #for name, value in e.scalars:
            #print(f"{name}: {value}")
