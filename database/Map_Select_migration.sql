ALTER TABLE location_measurements
    ADD COLUMN IF NOT EXISTS location_key TEXT;

ALTER TABLE cell_measurements
    ADD COLUMN IF NOT EXISTS cell_key TEXT;

CREATE UNIQUE INDEX IF NOT EXISTS ux_location_measurements_location_key
    ON location_measurements(location_key);

CREATE UNIQUE INDEX IF NOT EXISTS ux_cell_measurements_cell_key
    ON cell_measurements(cell_key);

CREATE INDEX IF NOT EXISTS idx_cell_measurements_location_id
    ON cell_measurements(location_id);
