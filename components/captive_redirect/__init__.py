"""Opens the ESP's own web page when a phone joins its hotspot.

Unlike ESPHome's captive_portal, which answers every request with its Wi-Fi
setup page, this only redirects requests for other hosts (the phone's
internet checks) to the normal web_server page.
"""

import esphome.codegen as cg
from esphome.components import socket, web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["wifi", "web_server_base"]
AUTO_LOAD = ["socket"]

captive_redirect_ns = cg.esphome_ns.namespace("captive_redirect")
CaptiveRedirect = captive_redirect_ns.class_("CaptiveRedirect", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CaptiveRedirect),
            cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(
                web_server_base.WebServerBase
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
)


def _final_validate(config):
    # One UDP socket for DNS, plus headroom for the phones' check requests
    socket.consume_sockets(3, "captive_redirect")(config)
    socket.consume_sockets(1, "captive_redirect", socket.SocketType.UDP)(config)
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    var = cg.new_Pvariable(config[CONF_ID], base)
    await cg.register_component(var, config)
