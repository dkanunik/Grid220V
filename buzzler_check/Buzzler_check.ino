const int PIN_BUZZER = 21;

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUZZER, OUTPUT);
  Serial.println("Buzzer forced test");
}

void loop() {
  Serial.println("ON");
  digitalWrite(PIN_BUZZER, HIGH);
  delay(800);

  Serial.println("OFF");
  digitalWrite(PIN_BUZZER, LOW);
  delay(1500);
}
