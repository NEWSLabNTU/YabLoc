import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image
from cv_bridge import CvBridge
import cv2


class NtuSegment(Node):
    def __init__(self):
        super().__init__("ntu_segment")

        # Create a subscriber to the /image_raw topic
        self.src_image_sub = self.create_subscription(
            Image,
            "src_image",  # Replace with your desired image topic name
            self.image_callback,
            10,  # QoS history depth
        )
        self.graph_segmented_pub = self.create_publisher(
            Image,
            "mask_image",  # Replace with your desired image topic name
            10,  # QoS history depth
        )
        self.segmented_image_pub = self.create_publisher(
            Image,
            "segmented_image",  # Replace with your desired image topic name
            10,  # QoS history depth
        )

        # Create a CvBridge object to convert ROS Image messages to OpenCV format
        self.bridge = CvBridge()

    def image_callback(self, msg):
        self.get_logger().info("Received an image!")

        # Convert the ROS Image message to an OpenCV image
        try:
            cv_image = self.bridge.imgmsg_to_cv2(msg, desired_encoding="bgr8")
        except Exception as e:
            self.get_logger().error(f"Failed to convert image: {e}")
            return

        # Display the image using OpenCV
        cv2.imshow("Received Image", cv_image)
        cv2.waitKey(1)  # Required to keep the OpenCV window responsive


def main(args=None):
    rclpy.init(args=args)

    image_subscriber = NtuSegment()

    try:
        rclpy.spin(image_subscriber)
    except KeyboardInterrupt:
        pass
    finally:
        image_subscriber.destroy_node()
        rclpy.shutdown()
        cv2.destroyAllWindows()  # Close OpenCV windows


if __name__ == "__main__":
    main()
