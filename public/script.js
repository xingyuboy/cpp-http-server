const tableBody = document.querySelector('#users tbody');
const status = document.querySelector('#status');

async function loadUsers() {
  status.textContent = 'Loading…';
  try {
    const response = await fetch('/api/users');
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();

    tableBody.innerHTML = '';
    for (const user of data.users) {
      const row = tableBody.insertRow();
      row.insertCell().textContent = user.id;
      row.insertCell().textContent = user.name;
      row.insertCell().textContent = user.email || '—';
    }
    status.textContent = `${data.count} user(s).`;
  } catch (error) {
    status.textContent = `Could not load users: ${error.message}`;
  }
}

async function addUser() {
  const name = document.querySelector('#name');
  const email = document.querySelector('#email');
  if (!name.value.trim()) {
    status.textContent = 'A name is required.';
    return;
  }

  const response = await fetch('/api/users', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ name: name.value.trim(), email: email.value.trim() }),
  });

  if (response.status === 201) {
    name.value = '';
    email.value = '';
    loadUsers();
  } else {
    const error = await response.json().catch(() => ({}));
    status.textContent = error.error || `Request failed with ${response.status}.`;
  }
}

document.querySelector('#add').addEventListener('click', addUser);
loadUsers();
