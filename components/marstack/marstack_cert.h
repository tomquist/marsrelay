#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32) && defined(USE_ESP_IDF)

namespace esphome {
namespace marstack {

// Self-signed certificate for the emulated cloud, with the Marstek cloud
// hostnames as subject alternative names.
//
// The battery does not check it. The firmware initializes this TLS path with
// mbedTLS authmode 0 and presents no client certificate of its own, so any
// certificate is accepted — which is what makes serving the upload endpoint
// possible at all. (The battery's *MQTT* path is the opposite: it verifies the
// broker against a CA in flash, which is why `mosquitto_broker` needs the
// battery pointed at it rather than intercepted.)
//
// The key is therefore public on purpose, exactly like the one
// `mosquitto_broker` embeds: it protects nothing, and generating one on the
// device would cost minutes of boot time for no gain. Do not reuse it
// elsewhere, and do not expose port 443 to the internet.
constexpr char MARSTACK_SERVER_CERT_PEM[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDjzCCAnegAwIBAgIUdcniAGn57AzT9vTvCJ/nyHx/yI8wDQYJKoZIhvcNAQEL\n"
    "BQAwIjEgMB4GA1UEAwwXYXBpLWV1Lm1hcnN0ZWtjbG91ZC5jb20wHhcNMjYwOTEz\n"
    "MDk1MDA4WhcNNDYwOTA4MDk1MDA4WjAiMSAwHgYDVQQDDBdhcGktZXUubWFyc3Rl\n"
    "a2Nsb3VkLmNvbTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAOIIM/i6\n"
    "qvxK6bFOO+v9jDDdoC3OoMC0ePN1YZmlPfco85TbDX6koxcTryzZmziU/F7r2wJY\n"
    "2tHKKV/Dbmbsuf5F+I2pJRWLyxVCBc5mxJqe4OsUEPunZSsBGQ3hy3OaUgDPyMdJ\n"
    "HmJPoAm4lgqnztYlh4wvT+8MbDgvuOEcNDZtpcvgvaKBUSi0jUfjrcyX8kfs3iAV\n"
    "T2yAKBLPbSoFscBBPPc8dbkoqdHXijUYEf4nI7gfn0jEuEOABiOrGsxNJIopq9ka\n"
    "09Oj4Zg3NOAt93TY2c6AP1Ejnqa8fL1iY3GdTrKOptcyCGSEqrm4sEB/yVJr8h0V\n"
    "M0hXdpWxfibtPCsCAwEAAaOBvDCBuTAdBgNVHQ4EFgQU8Hp/VNgyTGRDzYhlgopt\n"
    "EZGy9tswHwYDVR0jBBgwFoAU8Hp/VNgyTGRDzYhlgoptEZGy9tswDwYDVR0TAQH/\n"
    "BAUwAwEB/zBmBgNVHREEXzBdghdhcGktZXUubWFyc3Rla2Nsb3VkLmNvbYISKi5t\n"
    "YXJzdGVrY2xvdWQuY29tghBtYXJzdGVrY2xvdWQuY29tgg4qLmhhbWVkYXRhLmNv\n"
    "bYIMaGFtZWRhdGEuY29tMA0GCSqGSIb3DQEBCwUAA4IBAQDgfc4xZPq1mZHkcLJP\n"
    "icfdJqIc2EtCMugHXWdw1VfqTdfiXGjO7Yg0kr45Uo/NBmO2ti/0SUz91LeqHlzL\n"
    "sl2/zxyZae0Ecq7puyBrBuWEa3/W2Kk19OhdBK7QwIIAPFRoD2FRo4UvG8CW+BcI\n"
    "dRnjwE5G2CqE+VIuyvybm/kLBo6YHRcDzPlKAS2p2CclsB+PxJo6vcDE+8RNMCyY\n"
    "AaCiDdcS0gWGFBp0mBjY6IPfgVGZSgcGpGFmplr97Vjet9pQVoFbIUO5C7z3+VZD\n"
    "mEdqVx7z4Pzwu6/KT9KZc3SlbDm7+yOOLMVM/TiUrO+irVlRKDWRNah1zIglq3Pu\n"
    "SzzA\n"
    "-----END CERTIFICATE-----\n";

constexpr char MARSTACK_SERVER_KEY_PEM[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDiCDP4uqr8Sumx\n"
    "Tjvr/Yww3aAtzqDAtHjzdWGZpT33KPOU2w1+pKMXE68s2Zs4lPxe69sCWNrRyilf\n"
    "w25m7Ln+RfiNqSUVi8sVQgXOZsSanuDrFBD7p2UrARkN4ctzmlIAz8jHSR5iT6AJ\n"
    "uJYKp87WJYeML0/vDGw4L7jhHDQ2baXL4L2igVEotI1H463Ml/JH7N4gFU9sgCgS\n"
    "z20qBbHAQTz3PHW5KKnR14o1GBH+JyO4H59IxLhDgAYjqxrMTSSKKavZGtPTo+GY\n"
    "NzTgLfd02NnOgD9RI56mvHy9YmNxnU6yjqbXMghkhKq5uLBAf8lSa/IdFTNIV3aV\n"
    "sX4m7TwrAgMBAAECggEACQ7XAZzD+DPcJpr5OsSU+2qynf71CfEF5i8HLGTbVVm0\n"
    "F2TkD8d+yXlHPbemiUVCdLxBjkDAIZXXFW/buv1cmdgjt+tnqkVU6/jYlecI2sE5\n"
    "pNw4UdP0/RE9g5Cs1IAZoonfrWxw7q3rMw5Y8PAZ+hKxN30sngHXHsDBA27zI8r8\n"
    "b1LP349MwbYO1EzagNXiY3mQbJT8FMAp+urBy6iYcWrn0HEbLye3e5PhYOeH1b+Q\n"
    "v/uoaFJpeoQgYds4p1fZcKJ0jiJxHGCuLzW7ZiwZYp9rNBXa3OD/3luNXGuKraX5\n"
    "1ibiOS9et6eFI1Zh9XYDKUB7ATgfMaQKFUHnHD7QhQKBgQD4b7mHIKWFDqZ2UMLp\n"
    "vL2beDRkqmrWU0dCmHEp2N8H9cl4wEKrjRbF8O7l2gGYZ02sVazkxFw2K2vQc6Fr\n"
    "xTy6AJANKkVgytg7wz2BklGsJItEvxF581DrMMwUdRDZDFt5kwt2aR46bfwHj/7z\n"
    "GLyCVHh2EazEQmzbQQsPhQBytwKBgQDo6dyrdoe61HZ30gjGcwkgmj84nH8SbPpe\n"
    "0CHADr7uZOnuZXhbf9mutVVHD/TyZTxRvuTy4uED8knQJahhcvSIbIlD9MNv2Bm+\n"
    "5Mdlrd52y/7f/dCok0ihXdAby0SLcd6axZ1fkxUGDG7+wAVZun3qnh+7GIPMEXrb\n"
    "Sh/NHrV+LQKBgQDJrVj9G0GDHHuBvNoeCTwbA5/0wGtuhbhplXr4L7gOrDbbsaft\n"
    "v+Fm2sn1Cd8Vq3bgmcR7CfSZfPJPDC9UX4+Gp4JJTbF4e/LBwSMjFnb6ucfdDQbQ\n"
    "6vcblkd1q/r7WA7CSN6bR4ZkhHh+YyTij4gofQ41Ou/3er0H2Gt0M9JDTQKBgBr4\n"
    "gcIlLqB22+USIEwCpCrvUaTXkmtqzS9PgKkBzCBE9UXS4DYQPv/ZJa/d7CppiKua\n"
    "pF5v4UiYtO1yfuYR7rkhgF+rJzp7VrfhPCnNEXbGNpRvissKix2MhH3DcwOGwxU/\n"
    "DvMzT/rsU6oSSExUqSIS/2+l7hUibQyZr8cL6E5dAoGAdpqgD9WMushnfkoMhuXq\n"
    "KLctoisEocU5YBlP4VgvT0bav1yxGbHWL/2K/XdMlB0Te+gjitGWzPQ82jtiqEq7\n"
    "TvcnoB3X0CEGjSJfkudTk15jpgvPuGF4RGgXUUu363RDTQRKw08Lt1oKMJOCa6ar\n"
    "QDMaOFDqcUn1QVobJLbUpf4=\n"
    "-----END PRIVATE KEY-----\n";

}  // namespace marstack
}  // namespace esphome

#endif
