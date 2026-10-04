# Copyright 2026 Giovanni Remigi
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
#    * Neither the name of the Giovanni Remigi nor the names of its
#      contributors may be used to endorse or promote products derived from
#      this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

"""Start the Freezer node with the parameters of config/freezer.yaml, and the board page."""

from pathlib import Path

from launch import LaunchDescription
from launch.actions import (
    DeclareLaunchArgument,
    ExecuteProcess,
    LogInfo,
    OpaqueFunction,
)
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.parameter_descriptions import ParameterValue
from launch_ros.substitutions import FindPackageShare

# The board page, built by bin/build.sh into web/dist of the repository. The
# launch file is installed as a link to its source, so the repository is found
# from where it really is.
DEFAULT_WEB_DIR = Path(__file__).resolve().parents[3] / "web" / "dist"


def serve_page(context):
    """Serve the built page over HTTP, if it was built."""
    web_dir = Path(LaunchConfiguration("web_dir").perform(context))
    port = LaunchConfiguration("web_port").perform(context)
    if not (web_dir / "index.html").is_file():
        return [LogInfo(msg=f"No board page in {web_dir}: build it with bin/build.sh.")]
    return [
        LogInfo(msg=f"The board page is on http://localhost:{port}"),
        ExecuteProcess(
            cmd=["python3", "-m", "http.server", port, "--directory", str(web_dir)],
            output="log",
        ),
    ]


def generate_launch_description():
    use_fake = LaunchConfiguration("use_fake")
    usb_port = LaunchConfiguration("usb_port")
    web = LaunchConfiguration("web")
    config = PathJoinSubstitution(
        [FindPackageShare("freezer_node"), "config", "freezer.yaml"]
    )
    return LaunchDescription(
        [
            DeclareLaunchArgument(
                "use_fake",
                default_value="true",
                description="Use a fake controller instead of the Freezer board.",
            ),
            DeclareLaunchArgument(
                "usb_port",
                default_value="/dev/ttyUSB0",
                description="Serial port of the Arduino Nano.",
            ),
            DeclareLaunchArgument(
                "web",
                default_value="true",
                description="Serve the board page, and rosbridge for it.",
            ),
            DeclareLaunchArgument(
                "web_port",
                default_value="8092",
                description="HTTP port of the board page.",
            ),
            DeclareLaunchArgument(
                "web_dir",
                default_value=str(DEFAULT_WEB_DIR),
                description="Folder of the built board page.",
            ),
            DeclareLaunchArgument(
                "rosbridge_port",
                default_value="9092",
                description="Port of the rosbridge of the board page.",
            ),
            Node(
                package="freezer_node",
                executable="freezer_node",
                name="freezer",
                output="screen",
                parameters=[
                    config,
                    {
                        "use_fake": ParameterValue(use_fake, value_type=bool),
                        "usb_port": usb_port,
                    },
                ],
            ),
            # The page talks to the node through rosbridge, which needs
            # freezer_msgs next to it.
            Node(
                package="rosbridge_server",
                executable="rosbridge_websocket",
                name="freezer_rosbridge",
                output="log",
                parameters=[
                    {
                        "port": ParameterValue(
                            LaunchConfiguration("rosbridge_port"), value_type=int
                        )
                    }
                ],
                condition=IfCondition(web),
            ),
            OpaqueFunction(function=serve_page, condition=IfCondition(web)),
        ]
    )
