#include "server.h"
#include "configuration.h"
#include "yuki/file_path.h"
#include "glog/logging.h"
#include "gflags/gflags.h"
#include <stdio.h>
#include <memory>

DEFINE_string(conf_file, "./yukino.conf", "YukinoDB configuration file path.");
DEFINE_int32(max_events, 1024, "Event loop's max number of events.");

int main(int argc, char *argv[]) {
    using fLS::FLAGS_conf_file;

    gflags::ParseCommandLineFlags(&argc, &argv, true);
    google::InitGoogleLogging(argv[0]);

    if (FLAGS_conf_file.empty()) {
        LOG(ERROR) << "conf file path empty";
        return 1;
    }

    yuki::FilePath conf_file(FLAGS_conf_file);

    bool is_exist = false;
    auto rv = conf_file.Exist(&is_exist);
    if (rv.Failed()) {
        LOG(ERROR) << rv.ToString();
        return 1;
    }
    if (!is_exist) {
        LOG(ERROR) << "conf file: " << conf_file.Get() << " not exist";
        return 1;
    }

    auto fp = fopen(conf_file.c_str(), "r");
    if (!fp) {
        PLOG(ERROR) << "can not open conf file: " << conf_file.Get();
        return 1;
    }
    std::unique_ptr<yukino::Configuration> conf(new yukino::Configuration);
    rv = conf->LoadFile(fp);
    fclose(fp);
    if (rv.Failed()) {
        LOG(ERROR) << rv.ToString();
        return 1;
    }

    yukino::Server server(FLAGS_conf_file, conf.get(), FLAGS_max_events);
    rv = server.Init();
    if (rv.Failed()) {
        LOG(ERROR) << rv.ToString();
        return 1;
    }

    server.Run();
    return 0;
}