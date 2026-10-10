import { bootstrapApplication } from '@angular/platform-browser';
import { appConfig } from './app/app.config';
import { App } from './app/app';
import { ExamplePage } from './app/example-page';

// One bundle, two pages: /example opens the persona-driven example run in its own tab.
const root = window.location.pathname.startsWith('/example') ? ExamplePage : App;
bootstrapApplication(root, appConfig)
  .catch((err) => console.error(err));
