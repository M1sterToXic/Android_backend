CREATE TABLE IF NOT EXISTS location_measurements (
    id SERIAL PRIMARY KEY,
    timestamp BIGINT NOT NULL,
    time_str VARCHAR(30),
    latitude FLOAT NOT NULL,
    longitude FLOAT NOT NULL,
    altitude FLOAT,
    accuracy FLOAT,
    total_rx BIGINT DEFAULT 0,
    total_tx BIGINT DEFAULT 0,
    total BIGINT DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS cell_measurements (
    id SERIAL PRIMARY KEY,
    location_id INTEGER REFERENCES location_measurements(id),
    timestamp BIGINT NOT NULL,
    cell_type VARCHAR(10) NOT NULL,
    mcc INTEGER,
    mnc INTEGER,
    pci INTEGER,
    tac INTEGER,
    lac INTEGER,
    cell_identity BIGINT,
    earfcn INTEGER,
    arfcn INTEGER,
    nrarfcn INTEGER,
    band INTEGER,
    rsrp INTEGER,
    rsrq INTEGER,
    rssi INTEGER,
    rssnr INTEGER,
    ss_rsrp INTEGER,
    ss_rsrq INTEGER,
    ss_sinr INTEGER,
    dbm INTEGER,
    asu_level INTEGER,
    cqi INTEGER,
    timing_advance INTEGER,
    timing_advance_micros BIGINT,
    bsic INTEGER,
    nci VARCHAR(50),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_location_timestamp
    ON location_measurements(timestamp);

CREATE INDEX IF NOT EXISTS idx_cell_timestamp
    ON cell_measurements(timestamp);

CREATE INDEX IF NOT EXISTS idx_cell_pci
    ON cell_measurements(pci);

CREATE INDEX IF NOT EXISTS idx_cell_type
    ON cell_measurements(cell_type);

CREATE OR REPLACE VIEW v_latest_measurements AS
SELECT
    lm.id,
    lm.timestamp,
    lm.time_str,
    lm.latitude,
    lm.longitude,
    lm.accuracy,
    lm.total_rx,
    lm.total_tx,
    lm.total,
    cm.cell_type,
    cm.mcc,
    cm.mnc,
    cm.pci,
    cm.rsrp,
    cm.rsrq,
    cm.ss_rsrp,
    cm.ss_rsrq,
    cm.ss_sinr,
    cm.dbm
FROM location_measurements lm
LEFT JOIN cell_measurements cm ON lm.id = cm.location_id
ORDER BY lm.timestamp DESC
LIMIT 100;
