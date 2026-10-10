import unittest
from unittest.mock import patch

from tools.librecad_library_insert_factor import (
    REGISTRY_SUBKEY,
    VALUE_NAME,
    RegistryEntry,
    main,
    normalize_factor,
    read_factor,
    set_factor,
)


class FakeKey:
    def __init__(self, values):
        self.values = values

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False


class FakeWinreg:
    HKEY_CURRENT_USER = "HKCU"
    KEY_QUERY_VALUE = 0x0001
    KEY_SET_VALUE = 0x0002
    KEY_WOW64_32KEY = 0x0200
    KEY_WOW64_64KEY = 0x0100
    REG_SZ = 1
    REG_DWORD = 4

    def __init__(self):
        self.views = {self.KEY_WOW64_32KEY: {}, self.KEY_WOW64_64KEY: {}}

    def _view(self, access):
        if access & self.KEY_WOW64_32KEY:
            return self.KEY_WOW64_32KEY
        if access & self.KEY_WOW64_64KEY:
            return self.KEY_WOW64_64KEY
        raise AssertionError("A registry view must be selected explicitly")

    def OpenKey(self, root, subkey, _reserved, access):
        self.assert_location(root, subkey)
        values = self.views[self._view(access)].get(subkey)
        if values is None:
            raise FileNotFoundError(subkey)
        return FakeKey(values)

    def CreateKeyEx(self, root, subkey, _reserved, access):
        self.assert_location(root, subkey)
        values = self.views[self._view(access)].setdefault(subkey, {})
        return FakeKey(values)

    @staticmethod
    def assert_location(root, subkey):
        if root != FakeWinreg.HKEY_CURRENT_USER or subkey != REGISTRY_SUBKEY:
            raise AssertionError(f"Unexpected registry target: {root}\\{subkey}")

    @staticmethod
    def QueryValueEx(key, value_name):
        if value_name not in key.values:
            raise FileNotFoundError(value_name)
        return key.values[value_name]

    @staticmethod
    def SetValueEx(key, value_name, _reserved, value_type, value):
        key.values[value_name] = (value, value_type)


class LibraryInsertFactorTests(unittest.TestCase):
    def setUp(self):
        self.registry = FakeWinreg()

    def test_normalize_factor_accepts_positive_finite_numbers(self):
        self.assertEqual(normalize_factor(" 10 "), "10")
        self.assertEqual(normalize_factor("0.25"), "0.25")
        self.assertEqual(normalize_factor("1e2"), "1e2")

    def test_normalize_factor_rejects_invalid_or_nonpositive_numbers(self):
        for value in ("0", "-1", "NaN", "Infinity", "not-a-number", ""):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    normalize_factor(value)

    def test_read_factor_uses_the_requested_registry_view(self):
        path = REGISTRY_SUBKEY
        self.registry.views[self.registry.KEY_WOW64_32KEY][path] = {
            VALUE_NAME: ("10", self.registry.REG_SZ)
        }
        self.registry.views[self.registry.KEY_WOW64_64KEY][path] = {
            VALUE_NAME: ("1", self.registry.REG_SZ)
        }

        self.assertEqual(
            read_factor(self.registry, self.registry.KEY_WOW64_32KEY),
            RegistryEntry("10", self.registry.REG_SZ),
        )
        self.assertEqual(
            read_factor(self.registry, self.registry.KEY_WOW64_64KEY),
            RegistryEntry("1", self.registry.REG_SZ),
        )

    def test_read_factor_returns_none_when_key_or_value_is_missing(self):
        self.assertIsNone(read_factor(self.registry, self.registry.KEY_WOW64_32KEY))

        self.registry.views[self.registry.KEY_WOW64_32KEY][REGISTRY_SUBKEY] = {}
        self.assertIsNone(read_factor(self.registry, self.registry.KEY_WOW64_32KEY))

    def test_set_factor_writes_reg_sz_only_to_32_bit_view_and_reads_back(self):
        path = REGISTRY_SUBKEY
        self.registry.views[self.registry.KEY_WOW64_64KEY][path] = {
            VALUE_NAME: ("10", self.registry.REG_SZ)
        }

        written = set_factor(self.registry, "1.0")

        self.assertEqual(written, RegistryEntry("1.0", self.registry.REG_SZ))
        self.assertEqual(
            self.registry.views[self.registry.KEY_WOW64_32KEY][path][VALUE_NAME],
            ("1.0", self.registry.REG_SZ),
        )
        self.assertEqual(
            self.registry.views[self.registry.KEY_WOW64_64KEY][path][VALUE_NAME],
            ("10", self.registry.REG_SZ),
        )

    def test_set_command_updates_fake_registry_when_explicitly_confirmed(self):
        result = main(["set", "2.5", "--yes"], registry=self.registry)

        self.assertEqual(result, 0)
        self.assertEqual(
            read_factor(self.registry, self.registry.KEY_WOW64_32KEY),
            RegistryEntry("2.5", self.registry.REG_SZ),
        )

    def test_set_command_rejects_invalid_input_without_writing(self):
        result = main(["set", "0", "--yes"], registry=self.registry)

        self.assertEqual(result, 2)
        self.assertIsNone(read_factor(self.registry, self.registry.KEY_WOW64_32KEY))

    def test_running_without_a_command_opens_the_gui(self):
        with patch(
            "tools.librecad_library_insert_factor.launch_gui",
            create=True,
            return_value=7,
        ) as launch_gui:
            try:
                result = main([], registry=self.registry)
            except SystemExit:
                result = -1

        self.assertEqual(result, 7)
        launch_gui.assert_called_once_with(self.registry)


if __name__ == "__main__":
    unittest.main()
