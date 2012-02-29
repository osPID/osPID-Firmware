double readThermistorTemp(int voltage)
{

    float R = (inputType==1? 10:100) / (1024.0/(float)voltage - 1);
      float steinhart;
    steinhart = R / THERMISTORNOMINAL;     // (R/Ro)
    steinhart = log(steinhart);                  // ln(R/Ro)
    steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
    steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
    steinhart = 1.0 / steinhart;                 // Invert
    steinhart -= 273.15;                         // convert to C

  return steinhart;
}
