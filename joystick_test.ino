
const int joystick1X = A0;
const int joystick1Y = A1;
const int joystick1Button = 2;

const int joystick2X = A2;
const int joystick2Y = A3;
const int joystick2Button = 3;

void setup() {
  Serial.begin(9600);

  pinMode(joystick1Button, INPUT_PULLUP);
  pinMode(joystick2Button, INPUT_PULLUP);
}

void loop() {
  int x1 = analogRead(joystick1X);
  int y1 = analogRead(joystick1Y);
  int b1 = digitalRead(joystick1Button);

  int x2 = analogRead(joystick2X);
  int y2 = analogRead(joystick2Y);
  int b2 = digitalRead(joystick2Button);

  Serial.print("Joystick 1: X=");
  Serial.print(x1);
  Serial.print(" Y=");
  Serial.print(y1);
  Serial.print(" Button=");
  Serial.println(b1 == LOW ? "PRESSED" : "RELEASED");

  Serial.print("Joystick 2: X=");
  Serial.print(x2);
  Serial.print(" Y=");
  Serial.print(y2);
  Serial.print(" Button=");
  Serial.println(b2 == LOW ? "PRESSED" : "RELEASED");

  Serial.println("----------------------");

  delay(1000);
}
