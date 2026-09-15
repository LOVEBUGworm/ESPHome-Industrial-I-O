import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_ADDRESS
from esphome.components import binary_sensor, switch

CONF_BINARY_SENSOR = "binary_sensor"
CONF_SWITCH = "switch"
CONF_COILS = "coils"

modbus_tcp_server_ns = cg.esphome_ns.namespace("modbus_tcp_server")

ModbusTCPServer = modbus_tcp_server_ns.class_("ModbusTCPServer", cg.Component)

COIL_SCHEMA = cv.Schema({
    cv.Required(CONF_ADDRESS): cv.int_range(min=0, max=10),  # 0–10 inclusive
    cv.Optional(CONF_BINARY_SENSOR): cv.use_id(binary_sensor.BinarySensor),
    cv.Optional(CONF_SWITCH): cv.use_id(switch.Switch),
}).add_extra(
    cv.has_at_least_one_key(CONF_BINARY_SENSOR, CONF_SWITCH)
)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(ModbusTCPServer),
    cv.Optional("port", default=502): cv.port,
    cv.Optional("unit_id", default=1): cv.int_range(min=1, max=247),
    cv.Optional(CONF_COILS): cv.ensure_list(COIL_SCHEMA),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_port(config["port"]))
    cg.add(var.set_unit_id(config["unit_id"]))

    if CONF_COILS in config:
        for coil in config[CONF_COILS]:
            addr = coil[CONF_ADDRESS]

            if CONF_BINARY_SENSOR in coil:
                sens = await cg.get_variable(coil[CONF_BINARY_SENSOR])
                cg.add(var.register_coil(addr, sens))

            if CONF_SWITCH in coil:
                sw = await cg.get_variable(coil[CONF_SWITCH])
                cg.add(var.register_switch(addr, sw))
