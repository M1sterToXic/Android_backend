#include <iostream>
#include <thread>

#include "app_context.h"

int main(int argc, char* argv[]) {
    AppContext app;

    std::cout << "[DB] Connecting to database..." << std::endl;
    if (!db_init(app.dbConnection, "localhost", "telecom_db", "postgres", "postgres")) {
        std::cerr << "[DB] Warning: failed to connect to PostgreSQL." << std::endl;
        std::cerr << "[DB] Application will run without database saving." << std::endl;
        std::cerr << "[DB] To connect, install PostgreSQL and run:" << std::endl;
        std::cerr << "[DB]   psql -U postgres -f database/init_db.sql" << std::endl;
    }

    std::thread server_thread(run_server, &app);
    std::thread gui_thread(run_gui, &app);

    gui_thread.join();
    app.running = false;
    server_thread.join();

    db_close(app.dbConnection);

    return 0;
}
