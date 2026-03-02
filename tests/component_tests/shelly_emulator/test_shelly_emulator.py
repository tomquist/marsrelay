from pathlib import Path


def test_shelly_emulator_component_generation(generate_main):
    root_config = Path(__file__).parents[3] / "tests" / "fixtures" / "shelly_emulator.yaml"
    main_cpp = generate_main(str(root_config))
    assert "shelly_emulator::ShellyEmulator" in main_cpp
