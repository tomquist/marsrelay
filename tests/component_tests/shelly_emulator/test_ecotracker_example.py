from pathlib import Path


def test_ecotracker_example_generation(generate_main):
    """The EcoTracker example in docs/ecotracker.md must stay a valid config."""
    root_config = Path(__file__).parents[3] / "tests" / "fixtures" / "ecotracker_shelly.yaml"
    main_cpp = generate_main(str(root_config))
    assert "shelly_emulator::ShellyEmulator" in main_cpp
    assert "/v1/json" in main_cpp
