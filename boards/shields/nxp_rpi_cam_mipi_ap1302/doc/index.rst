.. _nxp_rpi_cam_mipi_ap1302:

NXP RPI-CAM-MIPI AP1302 Camera Module
###############################

Overview
********

The RPi-CAM-MIPI accessory board is a MIPI-CSI camera module adapter, AR0144 CMOS image sensor with ONSEMI IAS
interface by default, which features 1/4-inch 1.0 Mp with an active-pixel array of 1280H x 800V. The bypassable
on-board ISP chip AP1302 allows it to be used with a wide range of SoCs. This accessory board connects to the
i.MX93 FRDM board through the 22P/0.5mm Pitch FPC cable.


Requirements
************

This shield can only be used with a board which provides a 22-pin RPI interface, such as i.MX 93 FRDM board.

Programming
***********

Set ``--shield nxp_rpi_cam_mipi_ap1302`` when you invoke ``west build``. For example:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/video/capture
   :board: frdm_imx93/mimx9352/a55
   :shield: nxp_rpi_cam_mipi_ap1302
   :goals: build

References
**********

.. target-notes::

.. _RPI-CAM-MIPI:
   https://www.nxp.com/design/design-center/development-boards-and-designs/ias-camera-to-rpi-camera-adapter:RPI-CAM-MIPI
