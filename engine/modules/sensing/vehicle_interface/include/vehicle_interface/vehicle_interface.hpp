#ifndef OPENVISION_VEHICLE_INTERFACE_HPP
#define OPENVISION_VEHICLE_INTERFACE_HPP


class VehicleInterface
{
public:
    VehicleInterface() = default;
    virtual ~VehicleInterface() = default;

    // Read vehicle speed via CAN frame
    virtual double read() = 0;

    // Send steering and acceleration via CAN frame
    virtual void write(double steering, double acceleration) = 0;
};


#endif //OPENVISION_VEHICLE_INTERFACE_HPP
