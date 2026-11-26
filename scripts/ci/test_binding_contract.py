import unittest
import re
import validate_binding_contract as binding


class BindingContractTests(unittest.TestCase):
    def test_current_sources_match(self):
        self.assertEqual([], binding.validate(*binding.inputs(), binding.contract()))

    def test_one_sided_signature_change_fails(self):
        values = binding.inputs()
        values[0] = values[0].replace('(*Physics3D_CheckSphere)(float x', '(*Physics3D_CheckSphere)(double x')
        self.assertTrue(binding.validate(*values, binding.contract()))

    def test_equal_sized_reordered_slots_fail(self):
        values = binding.inputs()
        values[1] = values[1].replace('Physics3D_GetGravity', 'SWAP').replace('Physics3D_SetGravity', 'Physics3D_GetGravity').replace('SWAP', 'Physics3D_SetGravity')
        self.assertTrue(binding.validate(*values, binding.contract()))

    def test_missing_disabled_module_slot_fails(self):
        values = binding.inputs()
        values[2] = values[2].replace('X(Physics3D_CheckSphere)', '')
        self.assertTrue(binding.validate(*values, binding.contract()))

    def test_mixed_layout_versions_fail(self):
        values = binding.inputs()
        values[3] = re.sub(r'(LayoutVersion\s*=\s*)(\d+)', lambda m: m[1] + str(int(m[2]) + 1), values[3])
        self.assertTrue(binding.validate(*values, binding.contract()))


if __name__ == '__main__':
    unittest.main()
